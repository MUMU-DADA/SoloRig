#pragma once

#include "content/behavior_typed_runtime.hpp"
#include "dsp/mna_solver.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace solo_rig {

enum class TypedBehaviorMnaRuntimeError {
    None,
    InvalidPlan,
    InvalidCircuit,
    InvalidBinding,
    InvalidRange,
    InvalidSampleRate,
    InvalidOversampling,
    CapacityExceeded,
    MissingBinding,
    NotPrepared,
    NonFinite,
    BehaviorFailure,
    MnaFailure,
};

struct TypedBehaviorMnaBinding final {
    std::size_t control_index{};
    BehaviorPortDomain source_domain{BehaviorPortDomain::ControlSignal};
    double source_minimum{};
    double source_maximum{1.0};
    double target_minimum{};
    double target_maximum{1.0};
};

class TypedBehaviorMnaRuntime final {
public:
    static constexpr std::size_t kMaximumBindings = 8U;

    TypedBehaviorMnaRuntime() = default;
    TypedBehaviorMnaRuntime(TypedBehaviorMnaRuntime&&) noexcept = default;
    TypedBehaviorMnaRuntime& operator=(TypedBehaviorMnaRuntime&&) noexcept = default;
    TypedBehaviorMnaRuntime(const TypedBehaviorMnaRuntime&) = delete;
    TypedBehaviorMnaRuntime& operator=(const TypedBehaviorMnaRuntime&) = delete;

    [[nodiscard]] TypedBehaviorMnaRuntimeError Prepare(
        const BehaviorTypedGraphPlan& behavior_plan,
        MnaPreparedCircuit circuit,
        std::span<const TypedBehaviorMnaBinding> bindings,
        double sample_rate_hz,
        std::uint32_t oversampling) noexcept;
    [[nodiscard]] TypedBehaviorMnaRuntimeError SetSampleRate(double sample_rate_hz) noexcept;
    [[nodiscard]] TypedBehaviorMnaRuntimeError Process(
        const TypedBehaviorInput& behavior_input, double& audio_output) noexcept;
    [[nodiscard]] TypedBehaviorMnaRuntimeError Process(
        double audio_input, double& audio_output) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] std::size_t BindingCount() const noexcept { return binding_count_; }

private:
    [[nodiscard]] TypedBehaviorMnaRuntimeError MapBehaviorError(
        TypedBehaviorRuntimeError error) const noexcept;
    [[nodiscard]] TypedBehaviorMnaRuntimeError MapMnaError(
        MnaSolveError error) const noexcept;
    [[nodiscard]] static bool HasOutputDomain(
        const BehaviorTypedGraphPlan& plan, BehaviorPortDomain domain) noexcept;
    [[nodiscard]] static double ReadOutput(
        const TypedBehaviorOutput& output, BehaviorPortDomain domain) noexcept;

    TypedBehaviorGraphRuntime behavior_{};
    MnaPreparedCircuit circuit_{};
    std::array<TypedBehaviorMnaBinding, kMaximumBindings> bindings_{};
    std::size_t binding_count_{};
    bool prepared_{};
};

} // namespace solo_rig
