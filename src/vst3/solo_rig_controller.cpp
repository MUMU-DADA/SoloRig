#include "vst3/solo_rig_controller.h"

#include "vst3/solo_rig_ids.h"

#include "base/source/fstreamer.h"

#include <algorithm>

namespace solo_rig::vst3 {

Steinberg::tresult PLUGIN_API SoloRigController::initialize(Steinberg::FUnknown* context) {
    const auto result = EditController::initialize(context);
    if (result != Steinberg::kResultOk) {
        return result;
    }

    parameters.addParameter(
        STR16("Output Gain"),
        STR16("norm"),
        0,
        0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kOutputGainId);
    parameters.addParameter(
        STR16("Drive"),
        STR16("norm"),
        0,
        0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kDriveId);
    parameters.addParameter(
        STR16("Bypass"),
        nullptr,
        1,
        0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsBypass,
        kBypassId);
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API SoloRigController::setComponentState(Steinberg::IBStream* state) {
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
        !stream.readInt32(bypass) || magic != kStateMagic ||
        (version != 1 && version != 2 && version != kStateVersion)) {
        return Steinberg::kResultFalse;
    }
    if (version >= 2 && !stream.readFloat(drive)) {
        return Steinberg::kResultFalse;
    }

    setParamNormalized(kOutputGainId, std::clamp(static_cast<Steinberg::Vst::ParamValue>(output_gain), 0.0, 1.0));
    setParamNormalized(kDriveId, std::clamp(static_cast<Steinberg::Vst::ParamValue>(drive), 0.0, 1.0));
    setParamNormalized(kBypassId, bypass == 0 ? 0.0 : 1.0);
    return Steinberg::kResultOk;
}

} // namespace solo_rig::vst3
