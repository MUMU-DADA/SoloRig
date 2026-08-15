#include "content/behavior_envelope_runtime.hpp"

#include <cmath>

namespace solo_rig {

TypedEnvelopeRuntimeError TypedEnvelopeRuntime::Prepare(
    const BehaviorTypedGraphPlan& plan, double sample_rate_hz,
    std::uint32_t oversampling) noexcept {
    prepared_ = false;
    size_ = 0U;
    if (plan.nodes.empty() || plan.nodes.size() > kMaximumEnvelopeNodes ||
        plan.routes.size() != 2U) {
        return plan.nodes.size() > kMaximumEnvelopeNodes ?
            TypedEnvelopeRuntimeError::CapacityExceeded : TypedEnvelopeRuntimeError::InvalidPlan;
    }
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return TypedEnvelopeRuntimeError::InvalidSampleRate;
    }
    if (oversampling == 0U || oversampling > 16U) {
        return TypedEnvelopeRuntimeError::InvalidOversampling;
    }
    const auto& node = plan.nodes.front();
    if (plan.topological_order.size() != 1U || plan.topological_order.front() != node.id ||
        node.lowering.kind != BehaviorKind::Envelope ||
        node.lowering.model_id != "generic-envelope-v1" ||
        node.lowering.status != WhiteboxPlanStatus::Ready) {
        return TypedEnvelopeRuntimeError::UnsupportedBlock;
    }
    bool detector_seen = false;
    bool output_seen = false;
    for (const auto& route : plan.routes) {
        if (route.source.empty() || route.source_port.empty() || route.destination.empty() ||
            route.destination_port.empty() || route.source == route.destination) {
            return TypedEnvelopeRuntimeError::InvalidPlan;
        }
        if (route.source == "input" && route.source_port == "audio" &&
            route.destination == node.id && route.destination_port == "detector") {
            if (detector_seen) return TypedEnvelopeRuntimeError::IncompatibleRoute;
            detector_seen = true;
        } else if (route.source == node.id && route.source_port == "out" &&
                   route.destination == "output" && route.destination_port == "control") {
            if (output_seen) return TypedEnvelopeRuntimeError::IncompatibleRoute;
            output_seen = true;
        } else {
            return TypedEnvelopeRuntimeError::IncompatibleRoute;
        }
    }
    if (!detector_seen || !output_seen) return TypedEnvelopeRuntimeError::InvalidPlan;
    if (envelope_.Prepare(GenericEnvelopeModel{}, sample_rate_hz, oversampling) !=
        GenericEnvelopeError::None) {
        return TypedEnvelopeRuntimeError::InvalidSampleRate;
    }
    size_ = 1U;
    prepared_ = true;
    Reset();
    return TypedEnvelopeRuntimeError::None;
}

TypedEnvelopeRuntimeError TypedEnvelopeRuntime::SetSampleRate(double sample_rate_hz) noexcept {
    if (!prepared_) return TypedEnvelopeRuntimeError::NotPrepared;
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return TypedEnvelopeRuntimeError::InvalidSampleRate;
    }
    return envelope_.SetSampleRate(sample_rate_hz) == GenericEnvelopeError::None ?
        TypedEnvelopeRuntimeError::None : TypedEnvelopeRuntimeError::InvalidSampleRate;
}

TypedEnvelopeRuntimeError TypedEnvelopeRuntime::Process(double audio_input,
                                                        double& control_output) noexcept {
    control_output = 0.0;
    if (!prepared_) return TypedEnvelopeRuntimeError::NotPrepared;
    if (!std::isfinite(audio_input)) return TypedEnvelopeRuntimeError::NonFinite;
    const auto result = envelope_.Process(audio_input, control_output);
    if (result != GenericEnvelopeError::None || !std::isfinite(control_output)) {
        control_output = 0.0;
        return result == GenericEnvelopeError::NonFinite ? TypedEnvelopeRuntimeError::NonFinite :
            TypedEnvelopeRuntimeError::InvalidPlan;
    }
    return TypedEnvelopeRuntimeError::None;
}

void TypedEnvelopeRuntime::Reset() noexcept {
    if (prepared_) envelope_.Reset();
}

} // namespace solo_rig
