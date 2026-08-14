#include "dsp/nonlinear_amplifier.hpp"

#include <cmath>

namespace solo_rig {

bool ValidateNonlinearAmplifierModel(const NonlinearAmplifierModel& model) noexcept {
    const bool valid_gain = model.kind == AmplifierKind::OpAmp
        ? std::isfinite(model.gain) && model.gain >= 1.0 && model.gain <= 1.0e12
        : model.kind == AmplifierKind::Ota && std::isfinite(model.gain) &&
            model.gain >= 1.0e-12 && model.gain <= 1.0e6;
    return valid_gain && std::isfinite(model.minimum_output) &&
           std::isfinite(model.maximum_output) && model.minimum_output >= -1.0e6 &&
           model.maximum_output <= 1.0e6 && model.minimum_output < model.maximum_output &&
           std::isfinite(model.max_step) && model.max_step >= 1.0e-20 && model.max_step <= 10.0;
}

NonlinearAmplifierEvaluation EvaluateNonlinearAmplifier(
    const NonlinearAmplifierModel& model, double control_positive, double control_negative) noexcept {
    if (!ValidateNonlinearAmplifierModel(model)) {
        return {.error = NonlinearDeviceError::InvalidModel};
    }
    if (!std::isfinite(control_positive) || !std::isfinite(control_negative)) {
        return {.error = NonlinearDeviceError::NonFinite};
    }
    const double differential = control_positive - control_negative;
    const double minimum_input = model.minimum_output / model.gain;
    const double maximum_input = model.maximum_output / model.gain;
    if (differential <= minimum_input) {
        return {.error = NonlinearDeviceError::None, .output = model.minimum_output};
    }
    if (differential >= maximum_input) {
        return {.error = NonlinearDeviceError::None, .output = model.maximum_output};
    }
    const double output = model.gain * differential;
    if (!std::isfinite(output)) {
        return {.error = NonlinearDeviceError::NonFinite};
    }
    NonlinearAmplifierEvaluation result{.error = NonlinearDeviceError::None, .output = output};
    result.jacobian[0] = model.gain;
    result.jacobian[1] = -model.gain;
    return result;
}

} // namespace solo_rig
