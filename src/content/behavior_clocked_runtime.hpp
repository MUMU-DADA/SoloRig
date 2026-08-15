#pragma once

#include "content/behavior_clocked.hpp"
#include "dsp/generic_bbd_clocked.hpp"
#include "dsp/generic_vco.hpp"

#include <cstddef>
#include <cstdint>

namespace solo_rig {

enum class TypedVcoBbdClockedRuntimeError {
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

struct TypedVcoBbdClockedModel final {
    GenericVcoModel vco{};
    GenericBbdClockedModel bbd{};
};

class TypedVcoBbdClockedRuntime final {
public:
    [[nodiscard]] TypedVcoBbdClockedRuntimeError Prepare(
        const BehaviorClockedGraphPlan& plan, double sample_rate_hz,
        std::uint32_t oversampling) noexcept;
    [[nodiscard]] TypedVcoBbdClockedRuntimeError Prepare(
        const BehaviorClockedGraphPlan& plan, const TypedVcoBbdClockedModel& model,
        double sample_rate_hz, std::uint32_t oversampling) noexcept;
    [[nodiscard]] TypedVcoBbdClockedRuntimeError SetSampleRate(
        double sample_rate_hz) noexcept;
    [[nodiscard]] TypedVcoBbdClockedRuntimeError Process(
        double audio_input, double& audio_output) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] std::size_t Size() const noexcept { return size_; }
    [[nodiscard]] double CurrentClockFrequencyHz() const noexcept {
        return bbd_.CurrentClockFrequencyHz();
    }

private:
    GenericVcoRuntime vco_{};
    GenericBbdClockedRuntime bbd_{};
    std::size_t size_{};
    bool prepared_{};
};

} // namespace solo_rig
