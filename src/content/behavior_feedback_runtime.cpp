#include "content/behavior_feedback_runtime.hpp"

#include <cmath>

namespace solo_rig {

namespace {

TypedVcoBbdFeedbackRuntimeError MapVco(GenericVcoError error) noexcept {
    switch (error) {
    case GenericVcoError::None: return TypedVcoBbdFeedbackRuntimeError::None;
    case GenericVcoError::InvalidSampleRate:
        return TypedVcoBbdFeedbackRuntimeError::InvalidSampleRate;
    case GenericVcoError::InvalidOversampling:
        return TypedVcoBbdFeedbackRuntimeError::InvalidOversampling;
    case GenericVcoError::NonFinite:
        return TypedVcoBbdFeedbackRuntimeError::NonFinite;
    case GenericVcoError::InvalidModel:
        return TypedVcoBbdFeedbackRuntimeError::UnsupportedBlock;
    case GenericVcoError::NotPrepared:
        return TypedVcoBbdFeedbackRuntimeError::NotPrepared;
    }
    return TypedVcoBbdFeedbackRuntimeError::InvalidPlan;
}

TypedVcoBbdFeedbackRuntimeError MapBbd(GenericBbdFeedbackError error) noexcept {
    switch (error) {
    case GenericBbdFeedbackError::None: return TypedVcoBbdFeedbackRuntimeError::None;
    case GenericBbdFeedbackError::InvalidSampleRate:
        return TypedVcoBbdFeedbackRuntimeError::InvalidSampleRate;
    case GenericBbdFeedbackError::InvalidOversampling:
        return TypedVcoBbdFeedbackRuntimeError::InvalidOversampling;
    case GenericBbdFeedbackError::NonFinite:
        return TypedVcoBbdFeedbackRuntimeError::NonFinite;
    case GenericBbdFeedbackError::InvalidModel:
        return TypedVcoBbdFeedbackRuntimeError::InvalidFeedbackGain;
    case GenericBbdFeedbackError::NotPrepared:
        return TypedVcoBbdFeedbackRuntimeError::NotPrepared;
    }
    return TypedVcoBbdFeedbackRuntimeError::InvalidPlan;
}

GenericBbdFeedbackModel FeedbackModel() noexcept {
    return {.bucket_count = 512U, .clock_frequency_hz = 100000.0,
        .minimum_clock_frequency_hz = 50000.0, .maximum_clock_frequency_hz = 200000.0,
        .clock_control_octaves = 1.0, .transfer_efficiency = 0.99,
        .feedback_gain = 0.35, .wet = 1.0, .dry = 0.0, .maximum_input = 1.0};
}

bool IsExactPlan(const BehaviorFeedbackGraphPlan& plan) noexcept {
    if (plan.nodes.size() != 2U || plan.routes.size() != 4U ||
        plan.topological_order.size() != 2U) return false;
    const auto& vco = plan.nodes[0];
    const auto& bbd = plan.nodes[1];
    if (vco.id != plan.topological_order[0] || bbd.id != plan.topological_order[1] ||
        vco.lowering.kind != BehaviorKind::Vco || vco.lowering.model_id != "generic-vco-v1" ||
        bbd.lowering.kind != BehaviorKind::Bbd || bbd.lowering.model_id != "generic-bbd-v1" ||
        vco.lowering.status != WhiteboxPlanStatus::Ready ||
        bbd.lowering.status != WhiteboxPlanStatus::Ready) return false;
    bool audio = false;
    bool clock = false;
    bool feedback = false;
    bool output = false;
    for (const auto& route : plan.routes) {
        if (route.source == "input" && route.source_port == "audio" &&
            route.destination == bbd.id && route.destination_port == "audio_in") {
            if (audio) return false;
            audio = true;
        } else if (route.source == vco.id && route.source_port == "out" &&
                   route.destination == bbd.id && route.destination_port == "clock") {
            if (clock) return false;
            clock = true;
        } else if (route.source == bbd.id && route.source_port == "out" &&
                   route.destination == bbd.id && route.destination_port == "feedback") {
            if (feedback) return false;
            feedback = true;
        } else if (route.source == bbd.id && route.source_port == "out" &&
                   route.destination == "output" && route.destination_port == "audio") {
            if (output) return false;
            output = true;
        } else {
            return false;
        }
    }
    return audio && clock && feedback && output;
}

} // namespace

TypedVcoBbdFeedbackRuntimeError TypedVcoBbdFeedbackRuntime::Prepare(
    const BehaviorFeedbackGraphPlan& plan, double sample_rate_hz,
    std::uint32_t oversampling) noexcept {
    prepared_ = false;
    size_ = 0U;
    vco_ = GenericVcoRuntime{};
    bbd_ = GenericBbdFeedbackRuntime{};
    if (!IsExactPlan(plan)) return TypedVcoBbdFeedbackRuntimeError::InvalidPlan;
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return TypedVcoBbdFeedbackRuntimeError::InvalidSampleRate;
    }
    if (oversampling == 0U || oversampling > 16U) {
        return TypedVcoBbdFeedbackRuntimeError::InvalidOversampling;
    }
    const auto vco_result = vco_.Prepare(GenericVcoModel{}, sample_rate_hz, oversampling);
    if (vco_result != GenericVcoError::None) return MapVco(vco_result);
    const auto bbd_result = bbd_.Prepare(FeedbackModel(), sample_rate_hz, oversampling);
    if (bbd_result != GenericBbdFeedbackError::None) return MapBbd(bbd_result);
    size_ = 2U;
    prepared_ = true;
    Reset();
    return TypedVcoBbdFeedbackRuntimeError::None;
}

TypedVcoBbdFeedbackRuntimeError TypedVcoBbdFeedbackRuntime::SetSampleRate(
    double sample_rate_hz) noexcept {
    if (!prepared_) return TypedVcoBbdFeedbackRuntimeError::NotPrepared;
    const auto vco_result = vco_.SetSampleRate(sample_rate_hz);
    if (vco_result != GenericVcoError::None) return MapVco(vco_result);
    return MapBbd(bbd_.SetSampleRate(sample_rate_hz));
}

TypedVcoBbdFeedbackRuntimeError TypedVcoBbdFeedbackRuntime::Process(
    double audio_input, double& audio_output) noexcept {
    audio_output = 0.0;
    if (!prepared_) return TypedVcoBbdFeedbackRuntimeError::NotPrepared;
    if (!std::isfinite(audio_input)) return TypedVcoBbdFeedbackRuntimeError::NonFinite;
    double clock_control{};
    const auto vco_result = vco_.ProcessGenerator(clock_control);
    if (vco_result != GenericVcoError::None) return MapVco(vco_result);
    const auto bbd_result = bbd_.Process(audio_input, clock_control, audio_output);
    if (bbd_result != GenericBbdFeedbackError::None) {
        audio_output = 0.0;
        return MapBbd(bbd_result);
    }
    return std::isfinite(audio_output) ? TypedVcoBbdFeedbackRuntimeError::None :
        TypedVcoBbdFeedbackRuntimeError::NonFinite;
}

void TypedVcoBbdFeedbackRuntime::Reset() noexcept {
    if (!prepared_) return;
    vco_.Reset();
    bbd_.Reset();
}

} // namespace solo_rig
