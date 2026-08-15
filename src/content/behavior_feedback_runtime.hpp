#pragma once

#include "content/behavior_feedback.hpp"
#include "dsp/generic_bbd_feedback.hpp"
#include "dsp/generic_vco.hpp"

#include <cstddef>
#include <cstdint>

namespace solo_rig {

enum class TypedVcoBbdFeedbackRuntimeError {
    None,
    InvalidPlan,
    InvalidSampleRate,
    InvalidOversampling,
    NotPrepared,
    NonFinite,
    UnsupportedBlock,
    IncompatibleRoute,
    InvalidFeedbackGain,
    CapacityExceeded,
};

class TypedVcoBbdFeedbackRuntime final {
public:
    [[nodiscard]] TypedVcoBbdFeedbackRuntimeError Prepare(
        const BehaviorFeedbackGraphPlan& plan, double sample_rate_hz,
        std::uint32_t oversampling) noexcept;
    [[nodiscard]] TypedVcoBbdFeedbackRuntimeError SetSampleRate(
        double sample_rate_hz) noexcept;
    [[nodiscard]] TypedVcoBbdFeedbackRuntimeError Process(
        double audio_input, double& audio_output) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] std::size_t Size() const noexcept { return size_; }
    [[nodiscard]] double PreviousFeedbackOutput() const noexcept {
        return bbd_.PreviousOutput();
    }

private:
    GenericVcoRuntime vco_{};
    GenericBbdFeedbackRuntime bbd_{};
    std::size_t size_{};
    bool prepared_{};
};

} // namespace solo_rig
