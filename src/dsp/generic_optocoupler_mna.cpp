#include "dsp/generic_optocoupler_mna.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

bool ValidateGenericOptocouplerMnaModel(
    const GenericOptocouplerMnaModel& model) noexcept {
    return ValidateGenericOptocouplerModel(model.optical) &&
        std::isfinite(model.emitter_resistance_ohms) &&
        model.emitter_resistance_ohms >= 1.0e-3 &&
        model.emitter_resistance_ohms <= 1.0e9;
}

GenericOptocouplerError EvaluateGenericOptocouplerMna(
    const GenericOptocouplerMnaModel& model, double emitter_drive_volts,
    double previous_illumination, double timestep_seconds,
    GenericOptocouplerMnaEvaluation& output) noexcept {
    output = {};
    if (!ValidateGenericOptocouplerMnaModel(model)) {
        return GenericOptocouplerError::InvalidModel;
    }
    if (!std::isfinite(timestep_seconds) || timestep_seconds <= 0.0) {
        return GenericOptocouplerError::InvalidSampleRate;
    }
    if (!std::isfinite(previous_illumination) || previous_illumination < 0.0 ||
        previous_illumination > 1.0) {
        return GenericOptocouplerError::NonFinite;
    }
    GenericOptocouplerEvaluation target{};
    const auto target_result = EvaluateGenericOptocoupler(
        model.optical, emitter_drive_volts, previous_illumination, target);
    if (target_result != GenericOptocouplerError::None) return target_result;
    const double time_constant = target.target_illumination > previous_illumination
        ? model.optical.attack_seconds : model.optical.release_seconds;
    const double alpha = time_constant == 0.0 ? 1.0 :
        timestep_seconds / (time_constant + timestep_seconds);
    if (!std::isfinite(alpha) || alpha <= 0.0 || alpha > 1.0) {
        return GenericOptocouplerError::InvalidSampleRate;
    }
    output.next_illumination = std::clamp(previous_illumination + alpha *
        (target.target_illumination - previous_illumination), 0.0, 1.0);
    GenericOptocouplerEvaluation resistance{};
    const auto resistance_result = EvaluateGenericOptocoupler(
        model.optical, emitter_drive_volts, output.next_illumination, resistance);
    if (resistance_result != GenericOptocouplerError::None) {
        output = {};
        return resistance_result;
    }
    output.ldr_resistance_ohms = resistance.resistance_ohms;
    if (!std::isfinite(output.next_illumination) ||
        !std::isfinite(output.ldr_resistance_ohms)) {
        output = {};
        return GenericOptocouplerError::NonFinite;
    }
    return GenericOptocouplerError::None;
}

} // namespace solo_rig
