#include "dsp/nonlinear_bjt_dynamic.hpp"

#include "dsp/nonlinear_exponential.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace solo_rig {
namespace {
constexpr double kReferenceTemperature = 25.0;
constexpr double kKelvinReference = 273.15 + kReferenceTemperature;

}

bool ValidateNonlinearBjtDynamicModel(const NonlinearBjtDynamicModel& model) noexcept {
    if (!ValidateNonlinearBjtModel(model.static_model) || !std::isfinite(model.early_voltage) ||
        model.early_voltage < 0.0 || model.early_voltage > 1.0e6 ||
        !std::isfinite(model.be_junction_capacitance) || model.be_junction_capacitance < 0.0 ||
        model.be_junction_capacitance > 1.0e-3 || !std::isfinite(model.bc_junction_capacitance) ||
        model.bc_junction_capacitance < 0.0 || model.bc_junction_capacitance > 1.0e-3 ||
        !std::isfinite(model.be_junction_potential) || model.be_junction_potential < 1.0e-3 ||
        model.be_junction_potential > 100.0 || !std::isfinite(model.bc_junction_potential) ||
        model.bc_junction_potential < 1.0e-3 || model.bc_junction_potential > 100.0 ||
        !std::isfinite(model.be_grading_coefficient) || model.be_grading_coefficient < 0.0 ||
        model.be_grading_coefficient > 1.0 || !std::isfinite(model.bc_grading_coefficient) ||
        model.bc_grading_coefficient < 0.0 || model.bc_grading_coefficient > 1.0 ||
        !std::isfinite(model.temperature_celsius) || model.temperature_celsius < -100.0 ||
        model.temperature_celsius > 200.0 || !std::isfinite(model.leakage_temperature_coefficient) ||
        model.leakage_temperature_coefficient < -0.1 || model.leakage_temperature_coefficient > 0.1) {
        return false;
    }
    return true;
}

NonlinearBjtDynamicEvaluation EvaluateNonlinearBjtDynamic(
    const NonlinearBjtDynamicModel& model, double collector, double base, double emitter,
    double previous_be_voltage, double previous_bc_voltage, double conductance_scale) noexcept {
    if (!ValidateNonlinearBjtDynamicModel(model)) return {.error = NonlinearDeviceError::InvalidModel};
    if (!std::isfinite(collector) || !std::isfinite(base) || !std::isfinite(emitter) ||
        !std::isfinite(previous_be_voltage) || !std::isfinite(previous_bc_voltage) ||
        !std::isfinite(conductance_scale) || conductance_scale < 0.0) {
        return {.error = NonlinearDeviceError::NonFinite};
    }
    const double polarity = model.static_model.polarity == BjtPolarity::Npn ? 1.0 : -1.0;
    const double temperature_kelvin = 273.15 + model.temperature_celsius;
    const double temperature_ratio = temperature_kelvin / kKelvinReference;
    const double thermal_voltage = model.static_model.thermal_voltage * temperature_ratio;
    const auto leakage = nonlinear_detail::BoundedExponential(
        model.leakage_temperature_coefficient * (model.temperature_celsius - kReferenceTemperature));
    const double saturation_current = model.static_model.saturation_current * leakage.value;
    if (!std::isfinite(thermal_voltage) || !std::isfinite(saturation_current) ||
        thermal_voltage <= 0.0 || saturation_current <= 0.0) return {.error = NonlinearDeviceError::NonFinite};

    const double vbe = polarity * (base - emitter);
    const double vbc = polarity * (base - collector);
    const double vce = polarity * (collector - emitter);
    const auto forward = nonlinear_detail::BoundedExponential(vbe / thermal_voltage);
    const auto reverse = nonlinear_detail::BoundedExponential(vbc / thermal_voltage);
    double fi = saturation_current * (forward.value - 1.0);
    double ri = saturation_current * (reverse.value - 1.0);
    double fg = saturation_current / thermal_voltage * forward.derivative_scale;
    double rg = saturation_current / thermal_voltage * reverse.derivative_scale;
    if (std::fabs(fi) >= model.static_model.max_current) { fi = std::clamp(fi, -model.static_model.max_current, model.static_model.max_current); fg = 0.0; }
    if (std::fabs(ri) >= model.static_model.max_current) { ri = std::clamp(ri, -model.static_model.max_current, model.static_model.max_current); rg = 0.0; }
    const double fa = model.static_model.forward_beta / (model.static_model.forward_beta + 1.0);
    const double ra = model.static_model.reverse_beta / (model.static_model.reverse_beta + 1.0);
    double factor = 1.0;
    double factor_derivative = 0.0;
    if (model.early_voltage > 0.0 && vce > 0.0) {
        const double limited_vce = std::min(vce, 1.0e6);
        factor = 1.0 + limited_vce / model.early_voltage;
        factor_derivative = vce < 1.0e6 ? 1.0 / model.early_voltage : 0.0;
    }
    const double ftransport = fi * factor;
    const double fconductance = fg * factor;
    const double currents[3] = {
        polarity * (fa * ftransport - ri),
        polarity * ((1.0 - fa) * ftransport + (1.0 - ra) * ri),
        polarity * (-ftransport + ra * ri)};
    double jacobian[3][3] = {
        {fa * fi * factor_derivative + rg, fa * fconductance - rg,
         -fa * fconductance - fa * fi * factor_derivative},
        {(1.0 - fa) * fi * factor_derivative - (1.0 - ra) * rg,
         (1.0 - fa) * fconductance + (1.0 - ra) * rg,
         -(1.0 - fa) * fconductance - (1.0 - fa) * fi * factor_derivative},
        {-fi * factor_derivative - ra * rg,
         -fconductance + ra * rg,
         fconductance + fi * factor_derivative}};

    auto add_cap = [&](double voltage, double previous, double c0, double potential, double grading,
                       int positive_row, int negative_row) {
        const double base_term = std::max(1.0 - voltage / potential, 0.05);
        const bool clamped = base_term <= 0.0500000001;
        const double capacitance = c0 * std::pow(base_term, -grading);
        const double dcap = clamped ? 0.0 : c0 * grading / potential * std::pow(base_term, -grading - 1.0);
        const double conductance = (capacitance + dcap * (voltage - previous)) * conductance_scale;
        const double current = capacitance * (voltage - previous) * conductance_scale;
        (void)positive_row;
        (void)negative_row;
        return std::array<double, 3>{current, conductance, capacitance};
    };
    const auto be = add_cap(vbe, previous_be_voltage, model.be_junction_capacitance,
                            model.be_junction_potential, model.be_grading_coefficient, 1, 2);
    const auto bc = add_cap(vbc, previous_bc_voltage, model.bc_junction_capacitance,
                            model.bc_junction_potential, model.bc_grading_coefficient, 1, 0);
    // Rebuild mutable current storage after the const transport array above.
    NonlinearBjtDynamicEvaluation result{};
    for (int i = 0; i < 3; ++i) result.currents[i] = currents[i];
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) result.jacobian[r][c] = jacobian[r][c];
    auto apply = [&](const std::array<double, 3>& cap, int positive, int negative) {
        result.currents[positive] += polarity * cap[0];
        result.currents[negative] -= polarity * cap[0];
        const double g = cap[1];
        result.jacobian[positive][positive] += g;
        result.jacobian[positive][negative] -= g;
        result.jacobian[negative][positive] -= g;
        result.jacobian[negative][negative] += g;
    };
    apply(be, 1, 2);
    apply(bc, 1, 0);
    result.be_capacitance = be[2];
    result.bc_capacitance = bc[2];
    for (const auto current : result.currents) if (!std::isfinite(current)) return {.error = NonlinearDeviceError::NonFinite};
    for (const auto& row : result.jacobian) for (const auto value : row) if (!std::isfinite(value)) return {.error = NonlinearDeviceError::NonFinite};
    result.error = NonlinearDeviceError::None;
    return result;
}

} // namespace solo_rig
