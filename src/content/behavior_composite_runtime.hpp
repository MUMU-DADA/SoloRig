#pragma once

#include "content/behavior_ports.hpp"
#include "dsp/generic_envelope.hpp"
#include "dsp/generic_vca.hpp"

#include <cstddef>
#include <cstdint>

namespace solo_rig {

enum class TypedEnvelopeVcaRuntimeError {
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

class TypedEnvelopeVcaCompositeRuntime final {
public:
    [[nodiscard]] TypedEnvelopeVcaRuntimeError Prepare(const BehaviorTypedGraphPlan& plan,
                                                       double sample_rate_hz,
                                                       std::uint32_t oversampling) noexcept;
    [[nodiscard]] TypedEnvelopeVcaRuntimeError SetSampleRate(double sample_rate_hz) noexcept;
    [[nodiscard]] TypedEnvelopeVcaRuntimeError Process(double audio_input,
                                                       double detector_input,
                                                       double& audio_output) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] std::size_t Size() const noexcept { return size_; }

private:
    GenericEnvelopeRuntime envelope_{};
    GenericVcaRuntime vca_{};
    std::size_t size_{};
    bool prepared_{};
};

} // namespace solo_rig
