#pragma once

#include "content/behavior_ports.hpp"
#include "dsp/generic_optocoupler.hpp"

#include <cstddef>
#include <cstdint>

namespace solo_rig {

enum class TypedOptocouplerRuntimeError {
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

class TypedOptocouplerRuntime final {
public:
    [[nodiscard]] TypedOptocouplerRuntimeError Prepare(const BehaviorTypedGraphPlan& plan,
                                                       double sample_rate_hz,
                                                       std::uint32_t oversampling) noexcept;
    [[nodiscard]] TypedOptocouplerRuntimeError SetSampleRate(double sample_rate_hz) noexcept;
    [[nodiscard]] TypedOptocouplerRuntimeError Process(double emitter_input,
                                                       double& resistance_output) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] std::size_t Size() const noexcept { return size_; }

private:
    GenericOptocouplerRuntime optocoupler_{};
    std::size_t size_{};
    bool prepared_{};
};

} // namespace solo_rig
