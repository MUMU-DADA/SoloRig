#include "content/behavior_clocked_runtime.hpp"

#include <cmath>

namespace solo_rig {

namespace {

TypedVcoBbdClockedRuntimeError MapVco(GenericVcoError error) noexcept {
    switch (error) {
    case GenericVcoError::None: return TypedVcoBbdClockedRuntimeError::None;
    case GenericVcoError::InvalidSampleRate:
        return TypedVcoBbdClockedRuntimeError::InvalidSampleRate;
    case GenericVcoError::InvalidOversampling:
        return TypedVcoBbdClockedRuntimeError::InvalidOversampling;
    case GenericVcoError::NonFinite:
        return TypedVcoBbdClockedRuntimeError::NonFinite;
    case GenericVcoError::InvalidModel:
        return TypedVcoBbdClockedRuntimeError::UnsupportedBlock;
    case GenericVcoError::NotPrepared:
        return TypedVcoBbdClockedRuntimeError::NotPrepared;
    }
    return TypedVcoBbdClockedRuntimeError::InvalidPlan;
}

TypedVcoBbdClockedRuntimeError MapBbd(GenericBbdClockedError error) noexcept {
    switch (error) {
    case GenericBbdClockedError::None: return TypedVcoBbdClockedRuntimeError::None;
    case GenericBbdClockedError::InvalidSampleRate:
        return TypedVcoBbdClockedRuntimeError::InvalidSampleRate;
    case GenericBbdClockedError::InvalidOversampling:
        return TypedVcoBbdClockedRuntimeError::InvalidOversampling;
    case GenericBbdClockedError::NonFinite:
        return TypedVcoBbdClockedRuntimeError::NonFinite;
    case GenericBbdClockedError::InvalidModel:
        return TypedVcoBbdClockedRuntimeError::UnsupportedBlock;
    case GenericBbdClockedError::NotPrepared:
        return TypedVcoBbdClockedRuntimeError::NotPrepared;
    }
    return TypedVcoBbdClockedRuntimeError::InvalidPlan;
}

GenericBbdClockedModel ClockedModel() noexcept {
    return {.bucket_count = 512U, .clock_frequency_hz = 100000.0,
        .minimum_clock_frequency_hz = 50000.0, .maximum_clock_frequency_hz = 200000.0,
        .clock_control_octaves = 1.0, .transfer_efficiency = 0.99, .feedback = 0.0,
        .wet = 1.0, .dry = 0.0, .maximum_input = 1.0};
}

bool IsExactPlan(const BehaviorClockedGraphPlan& plan) noexcept {
    if (plan.nodes.size() != 2U || plan.routes.size() != 3U ||
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
                   route.destination == "output" && route.destination_port == "audio") {
            if (output) return false;
            output = true;
        } else {
            return false;
        }
    }
    return audio && clock && output;
}

} // namespace

TypedVcoBbdClockedRuntimeError TypedVcoBbdClockedRuntime::Prepare(
    const BehaviorClockedGraphPlan& plan, double sample_rate_hz,
    std::uint32_t oversampling) noexcept {
    prepared_ = false;
    size_ = 0U;
    vco_ = GenericVcoRuntime{};
    bbd_ = GenericBbdClockedRuntime{};
    if (!IsExactPlan(plan)) return TypedVcoBbdClockedRuntimeError::InvalidPlan;
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return TypedVcoBbdClockedRuntimeError::InvalidSampleRate;
    }
    if (oversampling == 0U || oversampling > 16U) {
        return TypedVcoBbdClockedRuntimeError::InvalidOversampling;
    }
    const auto vco_result = vco_.Prepare(GenericVcoModel{}, sample_rate_hz, oversampling);
    if (vco_result != GenericVcoError::None) return MapVco(vco_result);
    const auto bbd_result = bbd_.Prepare(ClockedModel(), sample_rate_hz, oversampling);
    if (bbd_result != GenericBbdClockedError::None) return MapBbd(bbd_result);
    size_ = 2U;
    prepared_ = true;
    Reset();
    return TypedVcoBbdClockedRuntimeError::None;
}

TypedVcoBbdClockedRuntimeError TypedVcoBbdClockedRuntime::SetSampleRate(
    double sample_rate_hz) noexcept {
    if (!prepared_) return TypedVcoBbdClockedRuntimeError::NotPrepared;
    const auto vco_result = vco_.SetSampleRate(sample_rate_hz);
    if (vco_result != GenericVcoError::None) return MapVco(vco_result);
    const auto bbd_result = bbd_.SetSampleRate(sample_rate_hz);
    return MapBbd(bbd_result);
}

TypedVcoBbdClockedRuntimeError TypedVcoBbdClockedRuntime::Process(
    double audio_input, double& audio_output) noexcept {
    audio_output = 0.0;
    if (!prepared_) return TypedVcoBbdClockedRuntimeError::NotPrepared;
    if (!std::isfinite(audio_input)) return TypedVcoBbdClockedRuntimeError::NonFinite;
    double clock_control{};
    const auto vco_result = vco_.ProcessGenerator(clock_control);
    if (vco_result != GenericVcoError::None) return MapVco(vco_result);
    const auto bbd_result = bbd_.Process(audio_input, clock_control, audio_output);
    if (bbd_result != GenericBbdClockedError::None) {
        audio_output = 0.0;
        return MapBbd(bbd_result);
    }
    return std::isfinite(audio_output) ? TypedVcoBbdClockedRuntimeError::None :
        TypedVcoBbdClockedRuntimeError::NonFinite;
}

void TypedVcoBbdClockedRuntime::Reset() noexcept {
    if (!prepared_) return;
    vco_.Reset();
    bbd_.Reset();
}

} // namespace solo_rig
