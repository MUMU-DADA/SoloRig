#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"

namespace solo_rig::vst3 {

class SoloRigProcessor final : public Steinberg::Vst::AudioEffect {
public:
    SoloRigProcessor();

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs,
        Steinberg::int32 num_inputs,
        Steinberg::Vst::SpeakerArrangement* outputs,
        Steinberg::int32 num_outputs) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new SoloRigProcessor());
    }

private:
    void ApplyParameterChanges(Steinberg::Vst::ProcessData& data);
    static double GainFromNormalizedValue(Steinberg::Vst::ParamValue normalized_value);

    Steinberg::Vst::ParamValue output_gain_ {0.5};
    bool bypass_ {false};
};

} // namespace solo_rig::vst3
