#include "vst3/solo_rig_processor.h"

#include "vst3/solo_rig_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

namespace solo_rig::vst3 {
namespace {

constexpr double kMinimumOutputGainDb = -24.0;
constexpr double kMaximumOutputGainDb = 24.0;

template <typename Sample>
void ProcessAudio(
    Steinberg::Vst::ProcessData& data,
    double gain,
    bool bypass,
    RigRuntime* runtime,
    AudioBuffer& processing_input,
    AudioBuffer& processing_output,
    bool runtime_prepared) {
    if (data.numInputs < 1 || data.numOutputs < 1 || data.inputs == nullptr || data.outputs == nullptr) {
        return;
    }

    const auto& input_bus = data.inputs[0];
    auto& output_bus = data.outputs[0];
    const auto channels = std::min(input_bus.numChannels, output_bus.numChannels);
    const auto can_apply_drive = !bypass && runtime != nullptr && runtime_prepared &&
        processing_input.ChannelCount() == static_cast<std::uint32_t>(channels) &&
        processing_input.Capacity() >= static_cast<std::uint32_t>(data.numSamples) &&
        processing_output.ChannelCount() == static_cast<std::uint32_t>(channels) &&
        processing_output.Capacity() >= static_cast<std::uint32_t>(data.numSamples);

    output_bus.silenceFlags = input_bus.silenceFlags;
    if (can_apply_drive) {
        processing_input.Clear(data.numSamples);
        for (Steinberg::int32 channel = 0; channel < channels; ++channel) {
            const auto* input = std::is_same_v<Sample, float>
                ? reinterpret_cast<const Sample*>(input_bus.channelBuffers32[channel])
                : reinterpret_cast<const Sample*>(input_bus.channelBuffers64[channel]);
            auto* destination = processing_input.ChannelData(static_cast<std::uint32_t>(channel));
            if (input != nullptr) {
                for (Steinberg::int32 sample = 0; sample < data.numSamples; ++sample) {
                    destination[sample] = static_cast<float>(input[sample]);
                }
            }
        }
        if (!runtime->Process(processing_input, processing_output, static_cast<std::uint32_t>(data.numSamples))) {
            return;
        }
    }

    for (Steinberg::int32 channel = 0; channel < channels; ++channel) {
        auto* input = std::is_same_v<Sample, float>
            ? reinterpret_cast<Sample*>(input_bus.channelBuffers32[channel])
            : reinterpret_cast<Sample*>(input_bus.channelBuffers64[channel]);
        auto* output = std::is_same_v<Sample, float>
            ? reinterpret_cast<Sample*>(output_bus.channelBuffers32[channel])
            : reinterpret_cast<Sample*>(output_bus.channelBuffers64[channel]);

        if (output == nullptr) {
            continue;
        }
        if (input == nullptr) {
            std::fill_n(output, data.numSamples, static_cast<Sample>(0));
            output_bus.silenceFlags |= static_cast<Steinberg::uint64>(1) << channel;
            continue;
        }
        if (can_apply_drive) {
            const auto* processed = processing_output.ChannelData(static_cast<std::uint32_t>(channel));
            for (Steinberg::int32 sample = 0; sample < data.numSamples; ++sample) {
                output[sample] = static_cast<Sample>(processed[sample] * gain);
            }
            continue;
        }
        const auto effective_gain = bypass ? 1.0 : gain;
        if (input == output && effective_gain == 1.0) {
            continue;
        }
        for (Steinberg::int32 sample = 0; sample < data.numSamples; ++sample) {
            output[sample] = static_cast<Sample>(input[sample] * effective_gain);
        }
    }

    for (Steinberg::int32 channel = channels; channel < output_bus.numChannels; ++channel) {
        auto* output = std::is_same_v<Sample, float>
            ? reinterpret_cast<Sample*>(output_bus.channelBuffers32[channel])
            : reinterpret_cast<Sample*>(output_bus.channelBuffers64[channel]);
        if (output != nullptr) {
            std::fill_n(output, data.numSamples, static_cast<Sample>(0));
        }
        output_bus.silenceFlags |= static_cast<Steinberg::uint64>(1) << channel;
    }
}

} // namespace

SoloRigProcessor::SoloRigProcessor() {
    setControllerClass(Steinberg::FUID::fromTUID(kControllerUid));
}

Steinberg::tresult PLUGIN_API SoloRigProcessor::initialize(Steinberg::FUnknown* context) {
    const auto result = AudioEffect::initialize(context);
    if (result != Steinberg::kResultOk) {
        return result;
    }

    addAudioInput(STR16("Input"), Steinberg::Vst::SpeakerArr::kStereo);
    addAudioOutput(STR16("Output"), Steinberg::Vst::SpeakerArr::kStereo);
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API SoloRigProcessor::setBusArrangements(
    Steinberg::Vst::SpeakerArrangement* inputs,
    Steinberg::int32 num_inputs,
    Steinberg::Vst::SpeakerArrangement* outputs,
    Steinberg::int32 num_outputs) {
    if (num_inputs != 1 || num_outputs != 1 || inputs == nullptr || outputs == nullptr ||
        inputs[0] != outputs[0]) {
        return Steinberg::kResultFalse;
    }
    const auto result = AudioEffect::setBusArrangements(inputs, num_inputs, outputs, num_outputs);
    if (result == Steinberg::kResultOk) {
        channel_count_ = Steinberg::Vst::SpeakerArr::getChannelCount(outputs[0]);
    }
    return result;
}

Steinberg::tresult PLUGIN_API SoloRigProcessor::setupProcessing(Steinberg::Vst::ProcessSetup& new_setup) {
    // VST3 calls setupProcessing while the host has stopped the audio thread.
    // Release the old graph before changing format so no slot remains owned by
    // a stale current/target pointer.
    ReleaseRuntime();
    const auto result = AudioEffect::setupProcessing(new_setup);
    if (result != Steinberg::kResultOk || new_setup.maxSamplesPerBlock <= 0 || new_setup.sampleRate <= 0.0 ||
        channel_count_ <= 0) {
        return result == Steinberg::kResultOk ? Steinberg::kResultFalse : result;
    }

    const auto format = AudioFormat{
        .sample_rate_hz = new_setup.sampleRate,
        .max_block_size = static_cast<std::uint32_t>(new_setup.maxSamplesPerBlock),
        .channel_count = static_cast<std::uint32_t>(channel_count_),
    };
    processing_input_ = AudioBuffer(format.channel_count, format.max_block_size);
    processing_output_ = AudioBuffer(format.channel_count, format.max_block_size);
    return BuildRuntime(format) ? Steinberg::kResultOk : Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API SoloRigProcessor::process(Steinberg::Vst::ProcessData& data) {
    static_cast<void>(ApplyPendingState());
    ApplyParameterChanges(data);
    if (data.numSamples <= 0) {
        return Steinberg::kResultOk;
    }

    const auto gain = GainFromNormalizedValue(audio_state_.output_gain);
    const auto bypass = audio_state_.bypass;
    if (data.symbolicSampleSize == Steinberg::Vst::kSample32) {
        ProcessAudio<float>(data,
                            gain,
                            bypass,
                            rig_runtime_.has_value() ? &*rig_runtime_ : nullptr,
                            processing_input_,
                            processing_output_,
                            rig_runtime_.has_value() && rig_runtime_->IsPrepared());
        return Steinberg::kResultOk;
    }
    if (data.symbolicSampleSize == Steinberg::Vst::kSample64) {
        ProcessAudio<double>(data,
                             gain,
                             bypass,
                             rig_runtime_.has_value() ? &*rig_runtime_ : nullptr,
                             processing_input_,
                             processing_output_,
                             rig_runtime_.has_value() && rig_runtime_->IsPrepared());
        return Steinberg::kResultOk;
    }
    return Steinberg::kInvalidArgument;
}

Steinberg::tresult PLUGIN_API SoloRigProcessor::setState(Steinberg::IBStream* state) {
    if (state == nullptr) {
        return Steinberg::kInvalidArgument;
    }

    Steinberg::IBStreamer stream(state, kLittleEndian);
    Steinberg::uint32 magic = 0;
    Steinberg::int32 version = 0;
    float output_gain = 0.0F;
    Steinberg::int32 bypass = 0;
    float drive = 0.0F;
    if (!stream.readInt32u(magic) || !stream.readInt32(version) || !stream.readFloat(output_gain) ||
        !stream.readInt32(bypass) || magic != kStateMagic || (version != 1 && version != kStateVersion)) {
        return Steinberg::kResultFalse;
    }
    if (version >= 2 && !stream.readFloat(drive)) {
        return Steinberg::kResultFalse;
    }

    // Both setState and the audio automation publisher acquire the odd
    // generation with CAS, so two writers cannot manufacture a false-even
    // snapshot. The control-side writer may wait briefly for the audio writer.
    auto generation = std::uint64_t{};
    for (;;) {
        generation = state_generation_.load(std::memory_order_acquire);
        if ((generation & 1U) != 0U) {
            continue;
        }
        if (state_generation_.compare_exchange_weak(generation,
                                                    generation + 1U,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire)) {
            break;
        }
    }
    output_gain_bits_.store(EncodeParam(std::clamp(static_cast<Steinberg::Vst::ParamValue>(output_gain), 0.0, 1.0)),
                            std::memory_order_relaxed);
    bypass_.store(bypass != 0, std::memory_order_relaxed);
    drive_bits_.store(EncodeParam(std::clamp(static_cast<Steinberg::Vst::ParamValue>(drive), 0.0, 1.0)),
                      std::memory_order_relaxed);
    state_generation_.fetch_add(1, std::memory_order_release);
    // State restoration may run on a non-audio thread. The even generation is
    // the hand-off; the audio callback applies it to current/target/pending
    // graphs at its control point without rebuilding or allocating.
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API SoloRigProcessor::getState(Steinberg::IBStream* state) {
    if (state == nullptr) {
        return Steinberg::kInvalidArgument;
    }

    const auto snapshot = ReadStateSnapshot();
    Steinberg::IBStreamer stream(state, kLittleEndian);
    if (!stream.writeInt32u(kStateMagic) || !stream.writeInt32(kStateVersion) ||
        !stream.writeFloat(static_cast<float>(snapshot.output_gain)) ||
        !stream.writeInt32(snapshot.bypass ? 1 : 0) ||
        !stream.writeFloat(static_cast<float>(snapshot.drive))) {
        return Steinberg::kResultFalse;
    }
    return Steinberg::kResultOk;
}

bool SoloRigProcessor::ApplyPendingState() noexcept {
    const auto first_generation = state_generation_.load(std::memory_order_acquire);
    if ((first_generation & 1U) != 0U) {
        return false;
    }
    const ParameterSnapshot snapshot{
        .output_gain = DecodeParam(output_gain_bits_.load(std::memory_order_relaxed)),
        .drive = DecodeParam(drive_bits_.load(std::memory_order_relaxed)),
        .bypass = bypass_.load(std::memory_order_relaxed),
    };
    const auto final_generation = state_generation_.load(std::memory_order_acquire);
    if (first_generation != final_generation || (final_generation & 1U) != 0U) {
        return false;
    }
    if (first_generation == audio_applied_generation_) {
        return false;
    }
    audio_state_ = snapshot;
    audio_applied_generation_ = first_generation;
    ConfigureDrive();
    return true;
}

SoloRigProcessor::ParameterSnapshot SoloRigProcessor::ReadStateSnapshot() const noexcept {
    for (;;) {
        const auto first_generation = state_generation_.load(std::memory_order_acquire);
        if ((first_generation & 1U) != 0U) {
            continue;
        }
        const ParameterSnapshot snapshot{
            .output_gain = DecodeParam(output_gain_bits_.load(std::memory_order_relaxed)),
            .drive = DecodeParam(drive_bits_.load(std::memory_order_relaxed)),
            .bypass = bypass_.load(std::memory_order_relaxed),
        };
        const auto final_generation = state_generation_.load(std::memory_order_acquire);
        if (first_generation == final_generation && (final_generation & 1U) == 0U) {
            return snapshot;
        }
    }
}

void SoloRigProcessor::ApplyParameterChanges(Steinberg::Vst::ProcessData& data) {
    if (data.inputParameterChanges == nullptr) {
        return;
    }

    for (Steinberg::int32 index = 0; index < data.inputParameterChanges->getParameterCount(); ++index) {
        auto* queue = data.inputParameterChanges->getParameterData(index);
        if (queue == nullptr || queue->getPointCount() <= 0) {
            continue;
        }

        Steinberg::int32 sample_offset = 0;
        Steinberg::Vst::ParamValue value = 0.0;
        if (queue->getPoint(queue->getPointCount() - 1, sample_offset, value) != Steinberg::kResultOk) {
            continue;
        }
        if (queue->getParameterId() == kOutputGainId) {
            audio_state_.output_gain = std::clamp(value, 0.0, 1.0);
        } else if (queue->getParameterId() == kBypassId) {
            audio_state_.bypass = value >= 0.5;
        } else if (queue->getParameterId() == kDriveId) {
            audio_state_.drive = std::clamp(value, 0.0, 1.0);
            ConfigureDrive();
        }
        PublishAudioState();
    }
}

void SoloRigProcessor::PublishAudioState() noexcept {
    auto generation = state_generation_.load(std::memory_order_acquire);
    if ((generation & 1U) != 0U ||
        !state_generation_.compare_exchange_strong(generation,
                                                   generation + 1U,
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_acquire)) {
        return;
    }
    output_gain_bits_.store(EncodeParam(audio_state_.output_gain), std::memory_order_relaxed);
    bypass_.store(audio_state_.bypass, std::memory_order_relaxed);
    drive_bits_.store(EncodeParam(audio_state_.drive), std::memory_order_relaxed);
    audio_applied_generation_ = state_generation_.fetch_add(1, std::memory_order_release) + 1U;
}

void SoloRigProcessor::ConfigureDrive() noexcept {
    if (rig_runtime_.has_value() && rig_runtime_->IsPrepared()) {
        static_cast<void>(rig_runtime_->SetParameter("drive-1", "drive", 1.0F + static_cast<float>(audio_state_.drive) * 15.0F));
    }
}

void SoloRigProcessor::ReleaseRuntime() noexcept {
    if (rig_runtime_.has_value()) {
        rig_runtime_->Shutdown();
        rig_runtime_.reset();
    } else {
        mailbox_.ResetForReconfigure();
    }
    prepared_runtime_.reset();
}

bool SoloRigProcessor::BuildRuntime(const AudioFormat& format) noexcept {
    try {
        const auto drive = ReadStateSnapshot().drive;
        RigPreset preset{
            .preset_id = "vst3-default",
            .name = "VST3 Default",
            .nodes = {PresetNode{.instance_id = "drive-1",
                                 .module_id = "org.mumu-dada.solorig.drive",
                                 .parameter_values = {{"drive", 1.0F + static_cast<float>(drive) * 15.0F},
                                                      {"outputGain", 1.0F},
                                                      {"diodeThreshold", 0.35F}}}},
            .connections = {},
        };
        PreparedPreset prepared{.preset_id = preset.preset_id, .preset = std::move(preset)};
        const auto built = RigRuntimeBuilder{}.Build(prepared, module_registry_, format);
        if (std::holds_alternative<RigBuildError>(built)) {
            return false;
        }
        prepared_runtime_.emplace(std::get<PreparedPreset>(built));
        rig_runtime_.emplace(mailbox_, format);
        if (!rig_runtime_->Prepare()) {
            ReleaseRuntime();
            return false;
        }
        mailbox_.Submit(*prepared_runtime_);
        return true;
    } catch (...) {
        ReleaseRuntime();
        return false;
    }
}

double SoloRigProcessor::GainFromNormalizedValue(Steinberg::Vst::ParamValue normalized_value) {
    const auto clamped = std::clamp(normalized_value, 0.0, 1.0);
    const auto gain_db = kMinimumOutputGainDb +
        (kMaximumOutputGainDb - kMinimumOutputGainDb) * clamped;
    return std::pow(10.0, gain_db / 20.0);
}

} // namespace solo_rig::vst3
