#include "content/behavior_composite_runtime.hpp"

#include <cmath>

namespace solo_rig {

namespace {

bool IsRoute(const BehaviorPortRouteSpec& route, std::string_view source,
            std::string_view source_port, std::string_view destination,
            std::string_view destination_port) noexcept {
    return route.source == source && route.source_port == source_port &&
        route.destination == destination && route.destination_port == destination_port;
}

} // namespace

TypedEnvelopeVcaRuntimeError TypedEnvelopeVcaCompositeRuntime::Prepare(
    const BehaviorTypedGraphPlan& plan, double sample_rate_hz,
    std::uint32_t oversampling) noexcept {
    prepared_ = false;
    size_ = 0U;
    if (plan.nodes.size() != 2U || plan.routes.size() != 4U ||
        plan.topological_order.size() != 2U) {
        return plan.nodes.size() > 2U || plan.routes.size() > 4U ?
            TypedEnvelopeVcaRuntimeError::CapacityExceeded :
            TypedEnvelopeVcaRuntimeError::InvalidPlan;
    }
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return TypedEnvelopeVcaRuntimeError::InvalidSampleRate;
    }
    if (oversampling == 0U || oversampling > 16U) {
        return TypedEnvelopeVcaRuntimeError::InvalidOversampling;
    }
    const auto& envelope = plan.nodes[0];
    const auto& vca = plan.nodes[1];
    if (envelope.lowering.kind != BehaviorKind::Envelope ||
        envelope.lowering.model_id != "generic-envelope-v1" ||
        envelope.lowering.status != WhiteboxPlanStatus::Ready ||
        vca.lowering.kind != BehaviorKind::Vca ||
        vca.lowering.model_id != "generic-vca-v1" ||
        vca.lowering.status != WhiteboxPlanStatus::Ready ||
        plan.topological_order[0] != envelope.id || plan.topological_order[1] != vca.id) {
        return TypedEnvelopeVcaRuntimeError::UnsupportedBlock;
    }
    if (!IsRoute(plan.routes[0], "input", "audio", vca.id, "audio_in") &&
        !IsRoute(plan.routes[1], "input", "audio", vca.id, "audio_in") &&
        !IsRoute(plan.routes[2], "input", "audio", vca.id, "audio_in") &&
        !IsRoute(plan.routes[3], "input", "audio", vca.id, "audio_in")) {
        return TypedEnvelopeVcaRuntimeError::IncompatibleRoute;
    }
    bool audio_seen = false;
    bool detector_seen = false;
    bool control_seen = false;
    bool output_seen = false;
    for (const auto& route : plan.routes) {
        if (route.source.empty() || route.source_port.empty() || route.destination.empty() ||
            route.destination_port.empty() || route.source == route.destination) {
            return TypedEnvelopeVcaRuntimeError::InvalidPlan;
        }
        if (IsRoute(route, "input", "audio", vca.id, "audio_in")) {
            if (audio_seen) return TypedEnvelopeVcaRuntimeError::IncompatibleRoute;
            audio_seen = true;
        } else if (IsRoute(route, "input", "detector", envelope.id, "detector")) {
            if (detector_seen) return TypedEnvelopeVcaRuntimeError::IncompatibleRoute;
            detector_seen = true;
        } else if (IsRoute(route, envelope.id, "out", vca.id, "control")) {
            if (control_seen) return TypedEnvelopeVcaRuntimeError::IncompatibleRoute;
            control_seen = true;
        } else if (IsRoute(route, vca.id, "out", "output", "audio")) {
            if (output_seen) return TypedEnvelopeVcaRuntimeError::IncompatibleRoute;
            output_seen = true;
        } else {
            return TypedEnvelopeVcaRuntimeError::IncompatibleRoute;
        }
    }
    if (!audio_seen || !detector_seen || !control_seen || !output_seen) {
        return TypedEnvelopeVcaRuntimeError::InvalidPlan;
    }
    if (envelope_.Prepare(GenericEnvelopeModel{}, sample_rate_hz, oversampling) !=
            GenericEnvelopeError::None ||
        vca_.Prepare(GenericVcaModel{}, sample_rate_hz, oversampling) !=
            GenericVcaError::None) {
        return TypedEnvelopeVcaRuntimeError::InvalidSampleRate;
    }
    size_ = 2U;
    prepared_ = true;
    Reset();
    return TypedEnvelopeVcaRuntimeError::None;
}

TypedEnvelopeVcaRuntimeError TypedEnvelopeVcaCompositeRuntime::SetSampleRate(
    double sample_rate_hz) noexcept {
    if (!prepared_) return TypedEnvelopeVcaRuntimeError::NotPrepared;
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return TypedEnvelopeVcaRuntimeError::InvalidSampleRate;
    }
    if (envelope_.SetSampleRate(sample_rate_hz) != GenericEnvelopeError::None ||
        vca_.SetSampleRate(sample_rate_hz) != GenericVcaError::None) {
        return TypedEnvelopeVcaRuntimeError::InvalidSampleRate;
    }
    return TypedEnvelopeVcaRuntimeError::None;
}

TypedEnvelopeVcaRuntimeError TypedEnvelopeVcaCompositeRuntime::Process(
    double audio_input, double detector_input, double& audio_output) noexcept {
    audio_output = 0.0;
    if (!prepared_) return TypedEnvelopeVcaRuntimeError::NotPrepared;
    if (!std::isfinite(audio_input) || !std::isfinite(detector_input)) {
        return TypedEnvelopeVcaRuntimeError::NonFinite;
    }
    double control{};
    if (envelope_.Process(detector_input, control) != GenericEnvelopeError::None ||
        !std::isfinite(control) ||
        vca_.Process(audio_input, control, audio_output) != GenericVcaError::None ||
        !std::isfinite(audio_output)) {
        audio_output = 0.0;
        return TypedEnvelopeVcaRuntimeError::NonFinite;
    }
    return TypedEnvelopeVcaRuntimeError::None;
}

void TypedEnvelopeVcaCompositeRuntime::Reset() noexcept {
    if (!prepared_) return;
    envelope_.Reset();
    vca_.Reset();
}

} // namespace solo_rig
