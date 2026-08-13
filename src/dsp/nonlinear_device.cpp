#include "dsp/nonlinear_device.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

namespace {

constexpr double kMinimumSaturationCurrent = 1.0e-30;
constexpr double kMaximumSaturationCurrent = 1.0e-3;
constexpr double kMinimumVoltageScale = 1.0e-6;
constexpr double kMaximumThermalVoltage = 1.0;
constexpr double kMaximumBreakdownVoltage = 100.0;
constexpr double kMaximumBreakdownCurrent = 1.0;
constexpr double kMaximumBreakdownSlope = 10.0;
constexpr double kMaximumStep = 10.0;
constexpr double kMaximumExponent = 60.0;

struct ExponentialTerm {
    double value{};
    double derivative_scale{};
};

ExponentialTerm BoundedExponential(double argument) noexcept {
    const auto limited = std::clamp(argument, -kMaximumExponent, kMaximumExponent);
    const auto value = std::exp(limited);
    return {.value = value,
            .derivative_scale = argument == limited ? value : 0.0};
}

} // namespace

bool ValidateNonlinearDeviceModel(const NonlinearDeviceModel& model) noexcept {
    if ((model.kind != NonlinearDeviceKind::Diode && model.kind != NonlinearDeviceKind::Zener) ||
        !std::isfinite(model.saturation_current) ||
        model.saturation_current < kMinimumSaturationCurrent ||
        model.saturation_current > kMaximumSaturationCurrent ||
        !std::isfinite(model.thermal_voltage) || model.thermal_voltage < kMinimumVoltageScale ||
        model.thermal_voltage > kMaximumThermalVoltage ||
        !std::isfinite(model.max_step) || model.max_step < kMinimumVoltageScale ||
        model.max_step > kMaximumStep) {
        return false;
    }
    if (model.kind == NonlinearDeviceKind::Diode) {
        return model.breakdown_voltage == 0.0 && model.breakdown_current == 0.0 &&
               model.breakdown_slope == 0.0;
    }
    return std::isfinite(model.breakdown_voltage) && model.breakdown_voltage >= 0.1 &&
           model.breakdown_voltage <= kMaximumBreakdownVoltage &&
           std::isfinite(model.breakdown_current) && model.breakdown_current >= 1.0e-12 &&
           model.breakdown_current <= kMaximumBreakdownCurrent &&
           std::isfinite(model.breakdown_slope) && model.breakdown_slope >= kMinimumVoltageScale &&
           model.breakdown_slope <= kMaximumBreakdownSlope;
}

NonlinearDeviceEvaluation EvaluateNonlinearDevice(
    const NonlinearDeviceModel& model, double voltage) noexcept {
    if (!ValidateNonlinearDeviceModel(model)) {
        return {.error = NonlinearDeviceError::InvalidModel};
    }
    if (!std::isfinite(voltage)) {
        return {.error = NonlinearDeviceError::NonFinite};
    }

    const auto forward = BoundedExponential(voltage / model.thermal_voltage);
    double current = model.saturation_current * (forward.value - 1.0);
    double jacobian = model.saturation_current / model.thermal_voltage * forward.derivative_scale;
    if (model.kind == NonlinearDeviceKind::Zener) {
        const auto reverse_argument = (-voltage - model.breakdown_voltage) / model.breakdown_slope;
        const auto reverse = BoundedExponential(reverse_argument);
        const auto zero_reverse = BoundedExponential(-model.breakdown_voltage / model.breakdown_slope);
        current -= model.breakdown_current * (reverse.value - zero_reverse.value);
        jacobian += model.breakdown_current / model.breakdown_slope * reverse.derivative_scale;
    }
    if (!std::isfinite(current) || !std::isfinite(jacobian) || jacobian < 0.0) {
        return {.error = NonlinearDeviceError::NonFinite};
    }
    return {.error = NonlinearDeviceError::None, .current = current, .jacobian = jacobian};
}

} // namespace solo_rig
