#pragma once

#include "core/audio_buffer.hpp"
#include "dsp/diode_clipper.hpp"
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
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& new_setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new SoloRigProcessor());
    }

private:
    void ApplyParameterChanges(Steinberg::Vst::ProcessData& data);
    void ConfigureDrive() noexcept;
    static double GainFromNormalizedValue(Steinberg::Vst::ParamValue normalized_value);

    Steinberg::Vst::ParamValue output_gain_ {0.5};
    Steinberg::Vst::ParamValue drive_ {0.0};
    bool bypass_ {false};
    Steinberg::int32 channel_count_ {2};
    bool drive_processor_prepared_ {};
    DiodeClipper drive_processor_;
    AudioBuffer processing_input_;
    AudioBuffer processing_output_;
};

} // namespace solo_rig::vst3
