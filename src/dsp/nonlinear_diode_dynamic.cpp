#include "dsp/nonlinear_diode_dynamic.hpp"

#include "dsp/nonlinear_exponential.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace solo_rig {

namespace {

constexpr double kReferenceTemperatureCelsius = 25.0;
constexpr double kAbsoluteZeroCelsius = -273.15;

double InvalidValue() noexcept { return std::numeric_limits<double>::quiet_NaN(); }

} // namespace

bool ValidateNonlinearDiodeDynamicModel(const NonlinearDiodeDynamicModel& model) noexcept {
    constexpr double maximum_capacitance = 1.0e-3;
    constexpr double maximum_potential = 100.0;
    constexpr double maximum_temperature_coefficient = 0.1;
    return ValidateNonlinearDeviceModel(model.static_model) &&
           std::isfinite(model.junction_capacitance) && model.junction_capacitance >= 0.0 &&
           model.junction_capacitance <= maximum_capacitance &&
           std::isfinite(model.junction_potential) && model.junction_potential >= 1.0e-3 &&
           model.junction_potential <= maximum_potential &&
           std::isfinite(model.grading_coefficient) && model.grading_coefficient >= 0.0 &&
           model.grading_coefficient <= 1.0 &&
           std::isfinite(model.temperature_celsius) && model.temperature_celsius >= -100.0 &&
           model.temperature_celsius <= 200.0 &&
           std::isfinite(model.leakage_current) && model.leakage_current >= 0.0 &&
           model.leakage_current <= 1.0 &&
           std::isfinite(model.leakage_temperature_coefficient) &&
           model.leakage_temperature_coefficient >= -maximum_temperature_coefficient &&
           model.leakage_temperature_coefficient <= maximum_temperature_coefficient &&
           model.temperature_celsius > kAbsoluteZeroCelsius;
}

NonlinearDiodeDynamicEvaluation EvaluateNonlinearDiodeDynamic(
    const NonlinearDiodeDynamicModel& model, double voltage, double previous_voltage,
    double conductance_scale) noexcept {
    if (!ValidateNonlinearDiodeDynamicModel(model)) {
        return {.error = NonlinearDeviceError::InvalidModel, .current = InvalidValue(),
                .jacobian = InvalidValue(), .capacitance = InvalidValue(),
                .capacitance_derivative = InvalidValue()};
    }
    if (!std::isfinite(voltage) || !std::isfinite(previous_voltage) ||
        !std::isfinite(conductance_scale) || conductance_scale <= 0.0) {
        return {.error = NonlinearDeviceError::NonFinite, .current = InvalidValue(),
                .jacobian = InvalidValue(), .capacitance = InvalidValue(),
                .capacitance_derivative = InvalidValue()};
    }

    const double temperature_ratio =
        (model.temperature_celsius - kAbsoluteZeroCelsius) /
        (kReferenceTemperatureCelsius - kAbsoluteZeroCelsius);
    const double thermal_voltage = model.static_model.thermal_voltage * temperature_ratio;
    const double leakage_exponent = std::clamp(
        model.leakage_temperature_coefficient *
            (model.temperature_celsius - kReferenceTemperatureCelsius), -50.0, 50.0);
    const double leakage_scale = std::exp(leakage_exponent);
    if (!std::isfinite(thermal_voltage) || thermal_voltage <= 0.0 || !std::isfinite(leakage_scale)) {
        return {.error = NonlinearDeviceError::NonFinite, .current = InvalidValue(),
                .jacobian = InvalidValue(), .capacitance = InvalidValue(),
                .capacitance_derivative = InvalidValue()};
    }

    auto static_model = model.static_model;
    static_model.thermal_voltage = thermal_voltage;
    static_model.saturation_current *= leakage_scale;
    const auto static_evaluation = EvaluateNonlinearDevice(static_model, voltage);
    if (!static_evaluation.Succeeded()) {
        return {.error = static_evaluation.error, .current = InvalidValue(),
                .jacobian = InvalidValue(), .capacitance = InvalidValue(),
                .capacitance_derivative = InvalidValue()};
    }

    const auto reverse = nonlinear_detail::BoundedExponential(-voltage / thermal_voltage);
    const double leakage = -model.leakage_current * leakage_scale * (reverse.value - 1.0);
    const double leakage_jacobian = model.leakage_current * leakage_scale / thermal_voltage *
        reverse.derivative_scale;

    double capacitance = 0.0;
    double capacitance_derivative = 0.0;
    if (model.junction_capacitance > 0.0) {
        constexpr double minimum_depletion_factor = 0.05;
        const double raw_factor = 1.0 - voltage / model.junction_potential;
        const double factor = std::max(raw_factor, minimum_depletion_factor);
        const double power = std::pow(factor, -model.grading_coefficient);
        capacitance = model.junction_capacitance * power;
        if (raw_factor > minimum_depletion_factor && model.grading_coefficient > 0.0) {
            capacitance_derivative = model.junction_capacitance * model.grading_coefficient /
                model.junction_potential * std::pow(factor, -model.grading_coefficient - 1.0);
        }
    }
    const double dynamic_current = (capacitance * (voltage - previous_voltage)) * conductance_scale;
    const double dynamic_jacobian = conductance_scale *
        (capacitance + capacitance_derivative * (voltage - previous_voltage));
    const double current = static_evaluation.current + leakage + dynamic_current;
    const double jacobian = static_evaluation.jacobian + leakage_jacobian + dynamic_jacobian;
    if (!std::isfinite(current) || !std::isfinite(jacobian) || jacobian < 0.0 ||
        !std::isfinite(capacitance) || !std::isfinite(capacitance_derivative)) {
        return {.error = NonlinearDeviceError::NonFinite, .current = InvalidValue(),
                .jacobian = InvalidValue(), .capacitance = InvalidValue(),
                .capacitance_derivative = InvalidValue()};
    }
    return {.error = NonlinearDeviceError::None, .current = current, .jacobian = jacobian,
            .capacitance = capacitance, .capacitance_derivative = capacitance_derivative};
}

} // namespace solo_rig
