#include "vst3/solo_rig_processor.h"

#include "vst3/solo_rig_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>

namespace solo_rig::vst3 {
namespace {

constexpr double kMinimumOutputGainDb = -24.0;
constexpr double kMaximumOutputGainDb = 24.0;

template <typename Sample>
void ProcessAudio(
    Steinberg::Vst::ProcessData& data,
    double gain,
    bool bypass) {
    if (data.numInputs < 1 || data.numOutputs < 1 || data.inputs == nullptr || data.outputs == nullptr) {
        return;
    }

    const auto& input_bus = data.inputs[0];
    auto& output_bus = data.outputs[0];
    const auto channels = std::min(input_bus.numChannels, output_bus.numChannels);
    const auto effective_gain = bypass ? 1.0 : gain;

    output_bus.silenceFlags = input_bus.silenceFlags;
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
    return AudioEffect::setBusArrangements(inputs, num_inputs, outputs, num_outputs);
}

Steinberg::tresult PLUGIN_API SoloRigProcessor::process(Steinberg::Vst::ProcessData& data) {
    ApplyParameterChanges(data);
    if (data.numSamples <= 0) {
        return Steinberg::kResultOk;
    }

    const auto gain = GainFromNormalizedValue(output_gain_);
    if (data.symbolicSampleSize == Steinberg::Vst::kSample32) {
        ProcessAudio<float>(data, gain, bypass_);
        return Steinberg::kResultOk;
    }
    if (data.symbolicSampleSize == Steinberg::Vst::kSample64) {
        ProcessAudio<double>(data, gain, bypass_);
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
    if (!stream.readInt32u(magic) || !stream.readInt32(version) || !stream.readFloat(output_gain) ||
        !stream.readInt32(bypass) || magic != kStateMagic || version != kStateVersion) {
        return Steinberg::kResultFalse;
    }

    output_gain_ = std::clamp(static_cast<Steinberg::Vst::ParamValue>(output_gain), 0.0, 1.0);
    bypass_ = bypass != 0;
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API SoloRigProcessor::getState(Steinberg::IBStream* state) {
    if (state == nullptr) {
        return Steinberg::kInvalidArgument;
    }

    Steinberg::IBStreamer stream(state, kLittleEndian);
    if (!stream.writeInt32u(kStateMagic) || !stream.writeInt32(kStateVersion) ||
        !stream.writeFloat(static_cast<float>(output_gain_)) ||
        !stream.writeInt32(bypass_ ? 1 : 0)) {
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
        }
    }
}

double SoloRigProcessor::GainFromNormalizedValue(Steinberg::Vst::ParamValue normalized_value) {
    const auto clamped = std::clamp(normalized_value, 0.0, 1.0);
    const auto gain_db = kMinimumOutputGainDb +
        (kMaximumOutputGainDb - kMinimumOutputGainDb) * clamped;
    return std::pow(10.0, gain_db / 20.0);
}

} // namespace solo_rig::vst3
