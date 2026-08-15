#pragma once

#include "content/behavior_ports.hpp"
#include "dsp/generic_envelope.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace solo_rig {

enum class TypedEnvelopeRuntimeError {
    None,
    InvalidPlan,
    InvalidSampleRate,
    InvalidOversampling,
    NotPrepared,
    NonFinite,
    UnsupportedBlock,
    IncompatibleRoute,
    CapacityExceeded,
};

class TypedEnvelopeRuntime final {
public:
    [[nodiscard]] TypedEnvelopeRuntimeError Prepare(const BehaviorTypedGraphPlan& plan,
                                                    double sample_rate_hz,
                                                    std::uint32_t oversampling) noexcept;
    [[nodiscard]] TypedEnvelopeRuntimeError SetSampleRate(double sample_rate_hz) noexcept;
    [[nodiscard]] TypedEnvelopeRuntimeError Process(double audio_input,
                                                    double& control_output) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] std::size_t Size() const noexcept { return size_; }

private:
    static constexpr std::size_t kMaximumEnvelopeNodes = 1U;
    GenericEnvelopeRuntime envelope_{};
    std::size_t size_{};
    bool prepared_{};
};

} // namespace solo_rig
