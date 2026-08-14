#include "dsp/dynamic_speaker_load.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace solo_rig {
namespace {

double SechSquared(double value) noexcept {
    const double tangent = std::tanh(value);
    return 1.0 - tangent * tangent;
}

} // namespace

bool ValidateDynamicSpeakerLoadModel(const DynamicSpeakerLoadModel& model) noexcept {
    return std::isfinite(model.voice_coil_resistance) &&
           model.voice_coil_resistance >= 1.0e-6 && model.voice_coil_resistance <= 1.0e9 &&
           std::isfinite(model.voice_coil_inductance) &&
           model.voice_coil_inductance >= 0.0 && model.voice_coil_inductance <= 1.0e3 &&
           std::isfinite(model.force_factor) && model.force_factor >= 0.0 &&
           model.force_factor <= 1.0e6 && std::isfinite(model.moving_mass) &&
           model.moving_mass >= 1.0e-9 && model.moving_mass <= 1.0e6 &&
           std::isfinite(model.mechanical_resistance) &&
           model.mechanical_resistance >= 0.0 && model.mechanical_resistance <= 1.0e9 &&
           std::isfinite(model.compliance) && model.compliance >= 1.0e-12 &&
           model.compliance <= 1.0e6 && std::isfinite(model.nonlinear_stiffness) &&
           model.nonlinear_stiffness >= 0.0 && model.nonlinear_stiffness <= 1.0e12 &&
           std::isfinite(model.nonlinear_displacement_scale) &&
           model.nonlinear_displacement_scale >= 1.0e-9 &&
           model.nonlinear_displacement_scale <= 1.0e3 &&
           std::isfinite(model.maximum_current) && model.maximum_current >= 1.0e-12 &&
           model.maximum_current <= 1.0e6 && std::isfinite(model.maximum_velocity) &&
           model.maximum_velocity >= 1.0e-9 && model.maximum_velocity <= 1.0e6 &&
           std::isfinite(model.maximum_displacement) &&
           model.maximum_displacement >= 1.0e-9 && model.maximum_displacement <= 1.0e6;
}

DynamicSpeakerLoadEvaluation EvaluateDynamicSpeakerLoad(
    const DynamicSpeakerLoadModel& model,
    double branch_current,
    const DynamicSpeakerLoadState& previous_state,
    double timestep_seconds) noexcept {
    DynamicSpeakerLoadEvaluation result{};
    if (!ValidateDynamicSpeakerLoadModel(model)) {
        result.error = NonlinearDeviceError::InvalidModel;
        return result;
    }
    if (!std::isfinite(branch_current) || !std::isfinite(previous_state.current) ||
        !std::isfinite(previous_state.velocity) || !std::isfinite(previous_state.displacement) ||
        !std::isfinite(timestep_seconds)) {
        result.error = NonlinearDeviceError::NonFinite;
        return result;
    }
    if (timestep_seconds <= 0.0 || timestep_seconds > 1.0e6 ||
        std::fabs(previous_state.current) > model.maximum_current ||
        std::fabs(previous_state.velocity) > model.maximum_velocity ||
        std::fabs(previous_state.displacement) > model.maximum_displacement) {
        result.error = NonlinearDeviceError::InvalidModel;
        return result;
    }

    const double normalized_current = branch_current / model.maximum_current;
    const double effective_current = model.maximum_current * std::tanh(normalized_current);
    const double current_derivative = SechSquared(normalized_current);
    const double mass_over_dt = model.moving_mass / timestep_seconds;
    const double linear_stiffness = 1.0 / model.compliance;

    const auto mechanics = [&](double velocity) noexcept {
        const double raw_displacement = previous_state.displacement + timestep_seconds * velocity;
        const double displacement = std::clamp(raw_displacement, -model.maximum_displacement,
                                               model.maximum_displacement);
        const double displacement_derivative_velocity =
            raw_displacement > -model.maximum_displacement &&
                    raw_displacement < model.maximum_displacement
                ? timestep_seconds
                : 0.0;
        const double normalized_displacement =
            displacement / model.nonlinear_displacement_scale;
        const double spring_force = linear_stiffness * displacement +
            model.nonlinear_stiffness * model.nonlinear_displacement_scale *
                std::tanh(normalized_displacement);
        const double spring_derivative = linear_stiffness +
            model.nonlinear_stiffness * SechSquared(normalized_displacement);
        const double residual = mass_over_dt * (velocity - previous_state.velocity) +
            model.mechanical_resistance * velocity + spring_force -
            model.force_factor * effective_current;
        const double derivative = mass_over_dt + model.mechanical_resistance +
            spring_derivative * displacement_derivative_velocity;
        return std::array<double, 4>{residual, derivative, displacement,
                                     displacement_derivative_velocity};
    };

    double lower = -model.maximum_velocity;
    double upper = model.maximum_velocity;
    const auto lower_evaluation = mechanics(lower);
    const auto upper_evaluation = mechanics(upper);
    double velocity{};
    bool velocity_clamped{};
    if (lower_evaluation[0] >= 0.0) {
        velocity = lower;
        velocity_clamped = true;
    } else if (upper_evaluation[0] <= 0.0) {
        velocity = upper;
        velocity_clamped = true;
    } else {
        velocity = std::clamp(previous_state.velocity, lower, upper);
        for (int iteration = 0; iteration < 48; ++iteration) {
            const auto evaluated = mechanics(velocity);
            if (evaluated[0] > 0.0) upper = velocity;
            else lower = velocity;
            if (std::fabs(evaluated[0]) <= 1.0e-12 ||
                upper - lower <= 1.0e-14 * (1.0 + std::fabs(velocity))) break;
            const double candidate = velocity - evaluated[0] / evaluated[1];
            velocity = candidate > lower && candidate < upper ? candidate : 0.5 * (lower + upper);
        }
    }

    const auto final_mechanics = mechanics(velocity);
    const double velocity_derivative_current = velocity_clamped ? 0.0 :
        model.force_factor * current_derivative / final_mechanics[1];
    const double inductance_over_dt = model.voice_coil_inductance / timestep_seconds;
    result.voltage = model.voice_coil_resistance * effective_current +
        inductance_over_dt * (effective_current - previous_state.current) +
        model.force_factor * velocity;
    result.voltage_derivative_current =
        (model.voice_coil_resistance + inductance_over_dt) * current_derivative +
        model.force_factor * velocity_derivative_current;
    result.next_state = {.current = effective_current, .velocity = velocity,
                         .displacement = final_mechanics[2]};
    if (!std::isfinite(result.voltage) || !std::isfinite(result.voltage_derivative_current) ||
        !std::isfinite(result.next_state.velocity) ||
        !std::isfinite(result.next_state.displacement)) {
        result.error = NonlinearDeviceError::NonFinite;
    }
    return result;
}

} // namespace solo_rig
