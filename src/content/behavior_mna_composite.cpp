#include "content/behavior_mna_composite.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace solo_rig {

namespace {

bool IsBindableDomain(BehaviorPortDomain domain) noexcept {
    return domain == BehaviorPortDomain::ControlSignal ||
        domain == BehaviorPortDomain::Resistance;
}

bool Within(double value, double minimum, double maximum) noexcept {
    constexpr double kRangeTolerance = 1.0e-12;
    return value >= minimum - kRangeTolerance && value <= maximum + kRangeTolerance;
}

} // namespace

TypedBehaviorMnaRuntimeError TypedBehaviorMnaRuntime::MapBehaviorError(
    TypedBehaviorRuntimeError error) const noexcept {
    switch (error) {
    case TypedBehaviorRuntimeError::None:
        return TypedBehaviorMnaRuntimeError::None;
    case TypedBehaviorRuntimeError::InvalidPlan:
    case TypedBehaviorRuntimeError::IncompatibleRoute:
        return TypedBehaviorMnaRuntimeError::InvalidPlan;
    case TypedBehaviorRuntimeError::InvalidSampleRate:
        return TypedBehaviorMnaRuntimeError::InvalidSampleRate;
    case TypedBehaviorRuntimeError::InvalidOversampling:
        return TypedBehaviorMnaRuntimeError::InvalidOversampling;
    case TypedBehaviorRuntimeError::CapacityExceeded:
        return TypedBehaviorMnaRuntimeError::CapacityExceeded;
    case TypedBehaviorRuntimeError::NotPrepared:
        return TypedBehaviorMnaRuntimeError::NotPrepared;
    case TypedBehaviorRuntimeError::NonFinite:
        return TypedBehaviorMnaRuntimeError::NonFinite;
    case TypedBehaviorRuntimeError::UnsupportedBlock:
        return TypedBehaviorMnaRuntimeError::InvalidPlan;
    }
    return TypedBehaviorMnaRuntimeError::InvalidPlan;
}

TypedBehaviorMnaRuntimeError TypedBehaviorMnaRuntime::MapMnaError(
    MnaSolveError error) const noexcept {
    switch (error) {
    case MnaSolveError::None:
        return TypedBehaviorMnaRuntimeError::None;
    case MnaSolveError::NotPrepared:
        return TypedBehaviorMnaRuntimeError::NotPrepared;
    case MnaSolveError::NonFinite:
        return TypedBehaviorMnaRuntimeError::NonFinite;
    case MnaSolveError::SingularMatrix:
    case MnaSolveError::NonConvergent:
        return TypedBehaviorMnaRuntimeError::MnaFailure;
    }
    return TypedBehaviorMnaRuntimeError::MnaFailure;
}

bool TypedBehaviorMnaRuntime::HasOutputDomain(
    const BehaviorTypedGraphPlan& plan, BehaviorPortDomain domain) noexcept {
    const auto output_name = domain == BehaviorPortDomain::ControlSignal ? "control" :
        domain == BehaviorPortDomain::Resistance ? "resistance" : "";
    if (output_name[0] == '\0') return false;
    return std::any_of(plan.routes.begin(), plan.routes.end(), [output_name](const auto& route) {
        return route.destination == "output" && route.destination_port == output_name;
    });
}

double TypedBehaviorMnaRuntime::ReadOutput(
    const TypedBehaviorOutput& output, BehaviorPortDomain domain) noexcept {
    switch (domain) {
    case BehaviorPortDomain::ControlSignal:
        return output.control;
    case BehaviorPortDomain::Resistance:
        return output.resistance;
    case BehaviorPortDomain::AudioSignal:
    case BehaviorPortDomain::DetectorInput:
    case BehaviorPortDomain::EmitterDrive:
        return output.audio;
    }
    return 0.0;
}

TypedBehaviorMnaRuntimeError TypedBehaviorMnaRuntime::Prepare(
    const BehaviorTypedGraphPlan& behavior_plan,
    MnaPreparedCircuit circuit,
    std::span<const TypedBehaviorMnaBinding> bindings,
    double sample_rate_hz,
    std::uint32_t oversampling) noexcept {
    prepared_ = false;
    binding_count_ = 0U;
    if (bindings.empty()) return TypedBehaviorMnaRuntimeError::MissingBinding;
    if (bindings.size() > kMaximumBindings) {
        return TypedBehaviorMnaRuntimeError::CapacityExceeded;
    }
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return TypedBehaviorMnaRuntimeError::InvalidSampleRate;
    }
    if (oversampling == 0U || oversampling > 16U) {
        return TypedBehaviorMnaRuntimeError::InvalidOversampling;
    }
    for (const auto& binding : bindings) {
        if (!IsBindableDomain(binding.source_domain) ||
            !HasOutputDomain(behavior_plan, binding.source_domain) ||
            !std::isfinite(binding.source_minimum) || !std::isfinite(binding.source_maximum) ||
            !std::isfinite(binding.target_minimum) || !std::isfinite(binding.target_maximum) ||
            binding.source_minimum >= binding.source_maximum ||
            binding.target_minimum >= binding.target_maximum) {
            return TypedBehaviorMnaRuntimeError::InvalidBinding;
        }
        const auto control_range = circuit.GetControlRange(binding.control_index);
        if (!control_range.has_value() ||
            !Within(binding.target_minimum, control_range->minimum, control_range->maximum) ||
            !Within(binding.target_maximum, control_range->minimum, control_range->maximum)) {
            return TypedBehaviorMnaRuntimeError::InvalidRange;
        }
        for (std::size_t prior = 0U; prior < binding_count_; ++prior) {
            if (bindings_[prior].control_index == binding.control_index) {
                return TypedBehaviorMnaRuntimeError::InvalidBinding;
            }
        }
        bindings_[binding_count_++] = binding;
    }

    const auto behavior_result = behavior_.Prepare(behavior_plan, sample_rate_hz, oversampling);
    if (behavior_result != TypedBehaviorRuntimeError::None) {
        binding_count_ = 0U;
        return MapBehaviorError(behavior_result);
    }
    if (!circuit.Prepare(sample_rate_hz)) {
        binding_count_ = 0U;
        return TypedBehaviorMnaRuntimeError::InvalidCircuit;
    }
    circuit_ = std::move(circuit);
    prepared_ = true;
    Reset();
    return TypedBehaviorMnaRuntimeError::None;
}

TypedBehaviorMnaRuntimeError TypedBehaviorMnaRuntime::SetSampleRate(
    double sample_rate_hz) noexcept {
    if (!prepared_) return TypedBehaviorMnaRuntimeError::NotPrepared;
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return TypedBehaviorMnaRuntimeError::InvalidSampleRate;
    }
    const auto behavior_result = behavior_.SetSampleRate(sample_rate_hz);
    if (behavior_result != TypedBehaviorRuntimeError::None) {
        return MapBehaviorError(behavior_result);
    }
    if (!circuit_.Prepare(sample_rate_hz)) {
        prepared_ = false;
        return TypedBehaviorMnaRuntimeError::InvalidCircuit;
    }
    Reset();
    return TypedBehaviorMnaRuntimeError::None;
}

TypedBehaviorMnaRuntimeError TypedBehaviorMnaRuntime::Process(
    const TypedBehaviorInput& behavior_input, double& audio_output) noexcept {
    audio_output = 0.0;
    if (!prepared_) return TypedBehaviorMnaRuntimeError::NotPrepared;
    TypedBehaviorOutput behavior_output{};
    const auto behavior_result = behavior_.Process(behavior_input, behavior_output);
    if (behavior_result != TypedBehaviorRuntimeError::None) {
        return MapBehaviorError(behavior_result);
    }
    for (std::size_t index = 0U; index < binding_count_; ++index) {
        const auto& binding = bindings_[index];
        const double source = ReadOutput(behavior_output, binding.source_domain);
        if (!std::isfinite(source)) return TypedBehaviorMnaRuntimeError::NonFinite;
        const double normalized = std::clamp(
            (source - binding.source_minimum) /
                (binding.source_maximum - binding.source_minimum),
            0.0, 1.0);
        const double target = binding.target_minimum + normalized *
            (binding.target_maximum - binding.target_minimum);
        if (!std::isfinite(target) || !circuit_.SetControlTarget(binding.control_index, target)) {
            return TypedBehaviorMnaRuntimeError::InvalidBinding;
        }
    }
    const auto solved = circuit_.Solve(behavior_input.audio);
    if (!solved.Succeeded()) return MapMnaError(solved.error);
    audio_output = solved.output_voltage;
    return std::isfinite(audio_output) ? TypedBehaviorMnaRuntimeError::None :
        TypedBehaviorMnaRuntimeError::NonFinite;
}

TypedBehaviorMnaRuntimeError TypedBehaviorMnaRuntime::Process(
    double audio_input, double& audio_output) noexcept {
    return Process(TypedBehaviorInput{.audio = audio_input}, audio_output);
}

void TypedBehaviorMnaRuntime::Reset() noexcept {
    if (!prepared_) return;
    behavior_.Reset();
    circuit_.Reset();
}

} // namespace solo_rig
