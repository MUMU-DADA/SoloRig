#include "dsp/dynamic_relay.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace solo_rig {

namespace {

bool FiniteState(const DynamicRelayState& state) noexcept {
    return std::isfinite(state.coil_current) && std::isfinite(state.position) &&
           std::isfinite(state.bounce_remaining_seconds) && std::isfinite(state.bounce_phase);
}

double EffectivePositionUnchecked(const DynamicRelayModel& model, const DynamicRelayState& state) noexcept {
    double position = state.position;
    if (state.bounce_remaining_seconds > 0.0 && model.bounce_duration_seconds > 0.0) {
        const double decay = std::clamp(
            state.bounce_remaining_seconds / model.bounce_duration_seconds, 0.0, 1.0);
        position += model.bounce_amplitude * decay * std::sin(state.bounce_phase);
    }
    return std::clamp(position, 0.0, 1.0);
}

} // namespace

bool ValidateDynamicRelayModel(const DynamicRelayModel& model) noexcept {
    const double values[] = {model.coil_resistance, model.coil_inductance,
        model.open_resistance, model.closed_resistance, model.pull_in_current,
        model.drop_out_current, model.operate_time_seconds, model.release_time_seconds,
        model.bounce_duration_seconds, model.bounce_frequency_hz, model.bounce_amplitude,
        model.maximum_coil_current};
    for (const double value : values) {
        if (!std::isfinite(value)) return false;
    }
    return model.coil_resistance > 0.0 && model.coil_resistance <= 1.0e12 &&
           model.coil_inductance >= 0.0 && model.coil_inductance <= 1.0e6 &&
           model.open_resistance > 0.0 && model.open_resistance <= 1.0e15 &&
           model.closed_resistance > 0.0 && model.closed_resistance <= 1.0e15 &&
           model.open_resistance > model.closed_resistance &&
           model.drop_out_current >= 0.0 && model.drop_out_current < model.pull_in_current &&
           model.pull_in_current <= 1.0e6 && model.operate_time_seconds > 0.0 &&
           model.operate_time_seconds <= 1.0e6 && model.release_time_seconds > 0.0 &&
           model.release_time_seconds <= 1.0e6 && model.bounce_duration_seconds >= 0.0 &&
           model.bounce_duration_seconds <= 60.0 && model.bounce_frequency_hz >= 0.0 &&
           model.bounce_frequency_hz <= 1.0e6 && model.bounce_amplitude >= 0.0 &&
           model.bounce_amplitude <= 1.0 && model.maximum_coil_current > 0.0 &&
           model.maximum_coil_current <= 1.0e6 &&
           (model.bounce_duration_seconds == 0.0 || model.bounce_frequency_hz > 0.0);
}

std::optional<double> DynamicRelayEffectivePosition(
    const DynamicRelayModel& model,
    const DynamicRelayState& state) noexcept {
    if (!ValidateDynamicRelayModel(model) || !FiniteState(state) || state.position < 0.0 ||
        state.position > 1.0 || state.bounce_remaining_seconds < 0.0 ||
        state.bounce_remaining_seconds > model.bounce_duration_seconds) {
        return std::nullopt;
    }
    return EffectivePositionUnchecked(model, state);
}

DynamicRelayEvaluation EvaluateDynamicRelay(const DynamicRelayModel& model,
                                            double coil_current,
                                            const DynamicRelayState& previous_state,
                                            double timestep_seconds) noexcept {
    if (!ValidateDynamicRelayModel(model)) {
        return {.error = DynamicRelayError::InvalidModel};
    }
    if (!std::isfinite(coil_current) || !FiniteState(previous_state) ||
        !std::isfinite(timestep_seconds) || timestep_seconds <= 0.0 ||
        std::fabs(coil_current) > model.maximum_coil_current ||
        previous_state.position < 0.0 || previous_state.position > 1.0 ||
        previous_state.bounce_remaining_seconds < 0.0 ||
        previous_state.bounce_remaining_seconds > model.bounce_duration_seconds) {
        return {.error = DynamicRelayError::NonFinite};
    }

    const double dynamic_resistance = model.coil_inductance / timestep_seconds;
    const double derivative = model.coil_resistance + dynamic_resistance;
    const double voltage = derivative * coil_current - dynamic_resistance * previous_state.coil_current;
    const double old_effective = EffectivePositionUnchecked(model, previous_state);
    const double contact_resistance = model.open_resistance +
        old_effective * (model.closed_resistance - model.open_resistance);

    DynamicRelayState next = previous_state;
    next.coil_current = coil_current;
    const double magnitude = std::fabs(coil_current);
    bool energized = previous_state.energized;
    if (!energized && magnitude >= model.pull_in_current) energized = true;
    if (energized && magnitude <= model.drop_out_current) energized = false;
    if (energized != previous_state.energized) {
        next.bounce_remaining_seconds = model.bounce_duration_seconds;
        next.bounce_phase = 0.0;
    }
    next.energized = energized;
    const double time_constant = energized ? model.operate_time_seconds : model.release_time_seconds;
    const double ratio = timestep_seconds / time_constant;
    const double alpha = ratio / (1.0 + ratio);
    next.position = std::clamp(previous_state.position +
        alpha * ((energized ? 1.0 : 0.0) - previous_state.position), 0.0, 1.0);
    if (next.bounce_remaining_seconds > 0.0) {
        next.bounce_remaining_seconds = std::max(0.0, next.bounce_remaining_seconds - timestep_seconds);
        next.bounce_phase = std::remainder(next.bounce_phase +
            2.0 * std::numbers::pi * model.bounce_frequency_hz * timestep_seconds,
            2.0 * std::numbers::pi);
        if (next.bounce_remaining_seconds == 0.0) next.bounce_phase = 0.0;
    }
    if (!std::isfinite(voltage) || !std::isfinite(derivative) ||
        !std::isfinite(contact_resistance) || contact_resistance <= 0.0 || !FiniteState(next)) {
        return {.error = DynamicRelayError::NonFinite};
    }
    return {.coil_voltage = voltage, .coil_voltage_derivative_current = derivative,
        .contact_resistance = contact_resistance, .next_state = next};
}

} // namespace solo_rig
