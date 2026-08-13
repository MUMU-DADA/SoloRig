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

bool ValidateNonlinearBjtModel(const NonlinearBjtModel& model) noexcept {
    return (model.polarity == BjtPolarity::Npn || model.polarity == BjtPolarity::Pnp) &&
           std::isfinite(model.saturation_current) && model.saturation_current >= 1.0e-30 &&
           model.saturation_current <= 1.0e-3 && std::isfinite(model.forward_beta) &&
           model.forward_beta >= 0.01 && model.forward_beta <= 1.0e6 &&
           std::isfinite(model.reverse_beta) && model.reverse_beta >= 0.01 &&
           model.reverse_beta <= 1.0e6 && std::isfinite(model.thermal_voltage) &&
           model.thermal_voltage >= 1.0e-6 && model.thermal_voltage <= 1.0 &&
           std::isfinite(model.max_current) && model.max_current >= 1.0e-12 &&
           model.max_current <= 1.0e6 && std::isfinite(model.max_step) &&
           model.max_step >= 1.0e-20 && model.max_step <= 10.0;
}

NonlinearBjtEvaluation EvaluateNonlinearBjt(
    const NonlinearBjtModel& model, double collector, double base, double emitter) noexcept {
    if (!ValidateNonlinearBjtModel(model)) {
        return {.error = NonlinearDeviceError::InvalidModel};
    }
    if (!std::isfinite(collector) || !std::isfinite(base) || !std::isfinite(emitter)) {
        return {.error = NonlinearDeviceError::NonFinite};
    }
    const auto polarity = model.polarity == BjtPolarity::Npn ? 1.0 : -1.0;
    const auto forward = BoundedExponential(polarity * (base - emitter) / model.thermal_voltage);
    const auto reverse = BoundedExponential(polarity * (base - collector) / model.thermal_voltage);
    auto forward_current = model.saturation_current * (forward.value - 1.0);
    auto reverse_current = model.saturation_current * (reverse.value - 1.0);
    auto forward_conductance = model.saturation_current / model.thermal_voltage * forward.derivative_scale;
    auto reverse_conductance = model.saturation_current / model.thermal_voltage * reverse.derivative_scale;
    if (std::fabs(forward_current) >= model.max_current) {
        forward_current = std::clamp(forward_current, -model.max_current, model.max_current);
        forward_conductance = 0.0;
    }
    if (std::fabs(reverse_current) >= model.max_current) {
        reverse_current = std::clamp(reverse_current, -model.max_current, model.max_current);
        reverse_conductance = 0.0;
    }
    const auto forward_alpha = model.forward_beta / (model.forward_beta + 1.0);
    const auto reverse_alpha = model.reverse_beta / (model.reverse_beta + 1.0);
    const double currents[3] = {
        polarity * (forward_alpha * forward_current - reverse_current),
        polarity * ((1.0 - forward_alpha) * forward_current +
                    (1.0 - reverse_alpha) * reverse_current),
        polarity * (-forward_current + reverse_alpha * reverse_current)};
    const double jacobian[3][3] = {
        {reverse_conductance,
         forward_alpha * forward_conductance - reverse_conductance,
         -forward_alpha * forward_conductance},
        {-(1.0 - reverse_alpha) * reverse_conductance,
         (1.0 - forward_alpha) * forward_conductance +
             (1.0 - reverse_alpha) * reverse_conductance,
         -(1.0 - forward_alpha) * forward_conductance},
        {-reverse_alpha * reverse_conductance,
         -forward_conductance + reverse_alpha * reverse_conductance,
         forward_conductance}};
    NonlinearBjtEvaluation result{.error = NonlinearDeviceError::None};
    for (std::size_t row = 0; row < 3U; ++row) {
        result.currents[row] = currents[row];
        for (std::size_t column = 0; column < 3U; ++column) {
            result.jacobian[row][column] = jacobian[row][column];
        }
    }
    for (const auto current : result.currents) {
        if (!std::isfinite(current)) {
            return {.error = NonlinearDeviceError::NonFinite};
        }
    }
    return result;
}

} // namespace solo_rig
