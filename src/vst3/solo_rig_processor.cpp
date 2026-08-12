#include "vst3/solo_rig_processor.h"

#include "vst3/solo_rig_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace solo_rig::vst3 {
namespace {

constexpr double kMinimumOutputGainDb = -24.0;
constexpr double kMaximumOutputGainDb = 24.0;

template <typename Sample>
void ProcessAudio(
    Steinberg::Vst::ProcessData& data,
    double gain,
    bool bypass,
    DiodeClipper& drive_processor,
    AudioBuffer& processing_input,
    AudioBuffer& processing_output,
    bool drive_processor_prepared) {
    if (data.numInputs < 1 || data.numOutputs < 1 || data.inputs == nullptr || data.outputs == nullptr) {
        return;
    }

    const auto& input_bus = data.inputs[0];
    auto& output_bus = data.outputs[0];
    const auto channels = std::min(input_bus.numChannels, output_bus.numChannels);
    const auto can_apply_drive = !bypass && drive_processor_prepared &&
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
        drive_processor.Process(processing_input, processing_output, data.numSamples);
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
    const auto result = AudioEffect::setupProcessing(new_setup);
    if (result != Steinberg::kResultOk || new_setup.maxSamplesPerBlock <= 0 || new_setup.sampleRate <= 0.0 ||
        channel_count_ <= 0) {
        drive_processor_prepared_ = false;
        return result == Steinberg::kResultOk ? Steinberg::kResultFalse : result;
    }

    const auto format = AudioFormat{
        .sample_rate_hz = new_setup.sampleRate,
        .max_block_size = static_cast<std::uint32_t>(new_setup.maxSamplesPerBlock),
        .channel_count = static_cast<std::uint32_t>(channel_count_),
    };
    processing_input_ = AudioBuffer(format.channel_count, format.max_block_size);
    processing_output_ = AudioBuffer(format.channel_count, format.max_block_size);
    drive_processor_prepared_ = drive_processor_.Prepare(format);
    return drive_processor_prepared_ ? Steinberg::kResultOk : Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API SoloRigProcessor::process(Steinberg::Vst::ProcessData& data) {
    ApplyParameterChanges(data);
    if (data.numSamples <= 0) {
        return Steinberg::kResultOk;
    }

    const auto gain = GainFromNormalizedValue(output_gain_);
    if (data.symbolicSampleSize == Steinberg::Vst::kSample32) {
        ProcessAudio<float>(data,
                            gain,
                            bypass_,
                            drive_processor_,
                            processing_input_,
                            processing_output_,
                            drive_processor_prepared_);
        return Steinberg::kResultOk;
    }
    if (data.symbolicSampleSize == Steinberg::Vst::kSample64) {
        ProcessAudio<double>(data,
                             gain,
                             bypass_,
                             drive_processor_,
                             processing_input_,
                             processing_output_,
                             drive_processor_prepared_);
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

    output_gain_ = std::clamp(static_cast<Steinberg::Vst::ParamValue>(output_gain), 0.0, 1.0);
    bypass_ = bypass != 0;
    drive_ = std::clamp(static_cast<Steinberg::Vst::ParamValue>(drive), 0.0, 1.0);
    ConfigureDrive();
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API SoloRigProcessor::getState(Steinberg::IBStream* state) {
    if (state == nullptr) {
        return Steinberg::kInvalidArgument;
    }

    Steinberg::IBStreamer stream(state, kLittleEndian);
    if (!stream.writeInt32u(kStateMagic) || !stream.writeInt32(kStateVersion) ||
        !stream.writeFloat(static_cast<float>(output_gain_)) ||
        !stream.writeInt32(bypass_ ? 1 : 0) || !stream.writeFloat(static_cast<float>(drive_))) {
        return Steinberg::kResultFalse;
    }
    return Steinberg::kResultOk;
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
            output_gain_ = std::clamp(value, 0.0, 1.0);
        } else if (queue->getParameterId() == kBypassId) {
            bypass_ = value >= 0.5;
        } else if (queue->getParameterId() == kDriveId) {
            drive_ = std::clamp(value, 0.0, 1.0);
            ConfigureDrive();
        }
    }
}

void SoloRigProcessor::ConfigureDrive() noexcept {
    auto settings = drive_processor_.Settings();
    settings.drive = 1.0F + static_cast<float>(drive_) * 15.0F;
    static_cast<void>(drive_processor_.SetSettings(settings));
}

double SoloRigProcessor::GainFromNormalizedValue(Steinberg::Vst::ParamValue normalized_value) {
    const auto clamped = std::clamp(normalized_value, 0.0, 1.0);
    const auto gain_db = kMinimumOutputGainDb +
        (kMaximumOutputGainDb - kMinimumOutputGainDb) * clamped;
    return std::pow(10.0, gain_db / 20.0);
}

} // namespace solo_rig::vst3
