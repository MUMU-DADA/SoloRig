#pragma once

#include "core/audio_buffer.hpp"
#include "core/rig_runtime.hpp"
#include "public.sdk/source/vst/vstaudioeffect.h"

#include <atomic>
#include <bit>
#include <cstdint>
#include <optional>

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
    struct ParameterSnapshot {
        Steinberg::Vst::ParamValue output_gain{0.5};
        Steinberg::Vst::ParamValue drive{0.0};
        bool bypass{};
    };

    void ApplyParameterChanges(Steinberg::Vst::ProcessData& data);
    [[nodiscard]] bool ApplyPendingState() noexcept;
    void PublishAudioState() noexcept;
    void ConfigureDrive() noexcept;
    void ReleaseRuntime() noexcept;
    [[nodiscard]] bool BuildRuntime(const AudioFormat& format) noexcept;
    static std::uint64_t EncodeParam(Steinberg::Vst::ParamValue value) noexcept {
        return std::bit_cast<std::uint64_t>(value);
    }
    static Steinberg::Vst::ParamValue DecodeParam(std::uint64_t value) noexcept {
        return std::bit_cast<Steinberg::Vst::ParamValue>(value);
    }
    [[nodiscard]] ParameterSnapshot ReadStateSnapshot() const noexcept;
    static double GainFromNormalizedValue(Steinberg::Vst::ParamValue normalized_value);

    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "VST3 real-time parameter mirrors must be lock-free");
    static_assert(std::atomic<bool>::is_always_lock_free,
                  "VST3 bypass parameter mirror must be lock-free");
    std::atomic<std::uint64_t> output_gain_bits_ {EncodeParam(0.5)};
    std::atomic<std::uint64_t> drive_bits_ {EncodeParam(0.0)};
    std::atomic<bool> bypass_ {false};
    std::atomic<std::uint64_t> state_generation_ {0};
    Steinberg::int32 channel_count_ {2};
    RigModuleRegistry module_registry_;
    RealtimePresetMailbox mailbox_;
    std::optional<PreparedPreset> prepared_runtime_;
    std::optional<RigRuntime> rig_runtime_;
    // Audio-thread-only coherent snapshot. Control calls publish atomics and
    // a seqlock generation; the callback adopts all three values together.
    ParameterSnapshot audio_state_{};
    std::uint64_t audio_applied_generation_{};
    AudioBuffer processing_input_;
    AudioBuffer processing_output_;
};

} // namespace solo_rig::vst3
