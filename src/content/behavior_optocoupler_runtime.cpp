#include "content/behavior_optocoupler_runtime.hpp"

#include <cmath>

namespace solo_rig {

TypedOptocouplerRuntimeError TypedOptocouplerRuntime::Prepare(
    const BehaviorTypedGraphPlan& plan, double sample_rate_hz,
    std::uint32_t oversampling) noexcept {
    prepared_ = false;
    size_ = 0U;
    if (plan.nodes.empty() || plan.nodes.size() > 1U || plan.routes.size() != 2U) {
        return plan.nodes.size() > 1U ? TypedOptocouplerRuntimeError::CapacityExceeded :
            TypedOptocouplerRuntimeError::InvalidPlan;
    }
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return TypedOptocouplerRuntimeError::InvalidSampleRate;
    }
    if (oversampling == 0U || oversampling > 16U) {
        return TypedOptocouplerRuntimeError::InvalidOversampling;
    }
    const auto& node = plan.nodes.front();
    if (plan.topological_order.size() != 1U || plan.topological_order.front() != node.id ||
        node.lowering.kind != BehaviorKind::Optocoupler ||
        node.lowering.model_id != "generic-optocoupler-v1" ||
        node.lowering.status != WhiteboxPlanStatus::Ready) {
        return TypedOptocouplerRuntimeError::UnsupportedBlock;
    }
    bool emitter_seen = false;
    bool output_seen = false;
    for (const auto& route : plan.routes) {
        if (route.source.empty() || route.source_port.empty() || route.destination.empty() ||
            route.destination_port.empty() || route.source == route.destination) {
            return TypedOptocouplerRuntimeError::InvalidPlan;
        }
        if (route.source == "input" && route.source_port == "emitter" &&
            route.destination == node.id && route.destination_port == "emitter") {
            if (emitter_seen) return TypedOptocouplerRuntimeError::IncompatibleRoute;
            emitter_seen = true;
        } else if (route.source == node.id && route.source_port == "out" &&
                   route.destination == "output" && route.destination_port == "resistance") {
            if (output_seen) return TypedOptocouplerRuntimeError::IncompatibleRoute;
            output_seen = true;
        } else {
            return TypedOptocouplerRuntimeError::IncompatibleRoute;
        }
    }
    if (!emitter_seen || !output_seen) return TypedOptocouplerRuntimeError::InvalidPlan;
    if (optocoupler_.Prepare(GenericOptocouplerModel{}, sample_rate_hz, oversampling) !=
        GenericOptocouplerError::None) {
        return TypedOptocouplerRuntimeError::InvalidSampleRate;
    }
    size_ = 1U;
    prepared_ = true;
    Reset();
    return TypedOptocouplerRuntimeError::None;
}

TypedOptocouplerRuntimeError TypedOptocouplerRuntime::SetSampleRate(
    double sample_rate_hz) noexcept {
    if (!prepared_) return TypedOptocouplerRuntimeError::NotPrepared;
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return TypedOptocouplerRuntimeError::InvalidSampleRate;
    }
    return optocoupler_.SetSampleRate(sample_rate_hz) == GenericOptocouplerError::None ?
        TypedOptocouplerRuntimeError::None : TypedOptocouplerRuntimeError::InvalidSampleRate;
}

TypedOptocouplerRuntimeError TypedOptocouplerRuntime::Process(
    double emitter_input, double& resistance_output) noexcept {
    resistance_output = 0.0;
    if (!prepared_) return TypedOptocouplerRuntimeError::NotPrepared;
    if (!std::isfinite(emitter_input)) return TypedOptocouplerRuntimeError::NonFinite;
    const auto result = optocoupler_.Process(emitter_input, resistance_output);
    if (result != GenericOptocouplerError::None || !std::isfinite(resistance_output)) {
        resistance_output = 0.0;
        return result == GenericOptocouplerError::NonFinite ?
            TypedOptocouplerRuntimeError::NonFinite : TypedOptocouplerRuntimeError::InvalidPlan;
    }
    return TypedOptocouplerRuntimeError::None;
}

void TypedOptocouplerRuntime::Reset() noexcept {
    if (prepared_) optocoupler_.Reset();
}

} // namespace solo_rig
