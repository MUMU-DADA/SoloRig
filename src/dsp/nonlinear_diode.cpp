#include "dsp/nonlinear_diode.hpp"

#include "dsp/nonlinear_exponential.hpp"

#include <cmath>

namespace solo_rig {

bool ValidateNonlinearDeviceModel(const NonlinearDeviceModel& model) noexcept {
    constexpr double minimum_saturation_current = 1.0e-30;
    constexpr double maximum_saturation_current = 1.0e-3;
    constexpr double minimum_voltage_scale = 1.0e-6;
    constexpr double maximum_thermal_voltage = 1.0;
    constexpr double maximum_breakdown_voltage = 100.0;
    constexpr double maximum_breakdown_current = 1.0;
    constexpr double maximum_breakdown_slope = 10.0;
    constexpr double maximum_step = 10.0;
    if ((model.kind != NonlinearDeviceKind::Diode && model.kind != NonlinearDeviceKind::Zener) ||
        !std::isfinite(model.saturation_current) ||
        model.saturation_current < minimum_saturation_current ||
        model.saturation_current > maximum_saturation_current ||
        !std::isfinite(model.thermal_voltage) || model.thermal_voltage < minimum_voltage_scale ||
        model.thermal_voltage > maximum_thermal_voltage ||
        !std::isfinite(model.max_step) || model.max_step < minimum_voltage_scale ||
        model.max_step > maximum_step) {
        return false;
    }
    if (model.kind == NonlinearDeviceKind::Diode) {
        return model.breakdown_voltage == 0.0 && model.breakdown_current == 0.0 &&
               model.breakdown_slope == 0.0;
    }
    return std::isfinite(model.breakdown_voltage) && model.breakdown_voltage >= 0.1 &&
           model.breakdown_voltage <= maximum_breakdown_voltage &&
           std::isfinite(model.breakdown_current) && model.breakdown_current >= 1.0e-12 &&
           model.breakdown_current <= maximum_breakdown_current &&
           std::isfinite(model.breakdown_slope) && model.breakdown_slope >= minimum_voltage_scale &&
           model.breakdown_slope <= maximum_breakdown_slope;
}

NonlinearDeviceEvaluation EvaluateNonlinearDevice(
    const NonlinearDeviceModel& model, double voltage) noexcept {
    if (!ValidateNonlinearDeviceModel(model)) {
        return {.error = NonlinearDeviceError::InvalidModel};
    }
    if (!std::isfinite(voltage)) {
        return {.error = NonlinearDeviceError::NonFinite};
    }

    const auto forward = nonlinear_detail::BoundedExponential(voltage / model.thermal_voltage);
    double current = model.saturation_current * (forward.value - 1.0);
    double jacobian = model.saturation_current / model.thermal_voltage * forward.derivative_scale;
    if (model.kind == NonlinearDeviceKind::Zener) {
        const auto reverse_argument = (-voltage - model.breakdown_voltage) / model.breakdown_slope;
        const auto reverse = nonlinear_detail::BoundedExponential(reverse_argument);
        const auto zero_reverse = nonlinear_detail::BoundedExponential(
            -model.breakdown_voltage / model.breakdown_slope);
        current -= model.breakdown_current * (reverse.value - zero_reverse.value);
        jacobian += model.breakdown_current / model.breakdown_slope * reverse.derivative_scale;
    }
    if (!std::isfinite(current) || !std::isfinite(jacobian) || jacobian < 0.0) {
        return {.error = NonlinearDeviceError::NonFinite};
    }
    return {.error = NonlinearDeviceError::None, .current = current, .jacobian = jacobian};
}

} // namespace solo_rig
