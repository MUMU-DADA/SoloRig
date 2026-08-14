#include "dsp/nonlinear_jfet_dynamic.hpp"

#include "dsp/nonlinear_exponential.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace solo_rig {
namespace {
constexpr double kReferenceTemperatureCelsius = 25.0;
constexpr double kReferenceKelvin = 298.15;
}

bool ValidateNonlinearJfetDynamicModel(const NonlinearJfetDynamicModel& model) noexcept {
    return ValidateNonlinearJfetModel(model.static_model) && std::isfinite(model.channel_length_modulation) &&
           model.channel_length_modulation >= 0.0 && model.channel_length_modulation <= 10.0 &&
           std::isfinite(model.temperature_celsius) && model.temperature_celsius >= -100.0 &&
           model.temperature_celsius <= 200.0 && std::isfinite(model.gate_saturation_current) &&
           model.gate_saturation_current >= 0.0 && model.gate_saturation_current <= 1.0 &&
           std::isfinite(model.gate_thermal_voltage) && model.gate_thermal_voltage >= 1.0e-6 &&
           model.gate_thermal_voltage <= 1.0 && std::isfinite(model.gate_junction_capacitance) &&
           model.gate_junction_capacitance >= 0.0 && model.gate_junction_capacitance <= 1.0e-3 &&
           std::isfinite(model.gate_junction_potential) && model.gate_junction_potential >= 1.0e-3 &&
           model.gate_junction_potential <= 100.0 && std::isfinite(model.gate_grading_coefficient) &&
           model.gate_grading_coefficient >= 0.0 && model.gate_grading_coefficient <= 1.0 &&
           std::isfinite(model.gate_leakage_temperature_coefficient) &&
           model.gate_leakage_temperature_coefficient >= -0.1 &&
           model.gate_leakage_temperature_coefficient <= 0.1;
}

NonlinearJfetDynamicEvaluation EvaluateNonlinearJfetDynamic(
    const NonlinearJfetDynamicModel& model, double drain, double gate, double source,
    double previous_gs_voltage, double previous_gd_voltage, double conductance_scale) noexcept {
    if (!ValidateNonlinearJfetDynamicModel(model)) return {.error = NonlinearDeviceError::InvalidModel};
    if (!std::isfinite(drain) || !std::isfinite(gate) || !std::isfinite(source) ||
        !std::isfinite(previous_gs_voltage) || !std::isfinite(previous_gd_voltage) ||
        !std::isfinite(conductance_scale) || conductance_scale < 0.0) return {.error = NonlinearDeviceError::NonFinite};

    const double polarity = model.static_model.polarity == JfetPolarity::NChannel ? 1.0 : -1.0;
    const double temperature_ratio = (273.15 + model.temperature_celsius) / kReferenceKelvin;
    const double pinch = model.static_model.pinch_off_voltage * std::sqrt(temperature_ratio);
    const double parameter = model.static_model.transconductance_parameter / temperature_ratio;
    const double normalized_limit = model.static_model.max_current / std::max(parameter, 1.0e-30);
    const double vgs = polarity * (gate - source);
    const double vds = polarity * (drain - source);
    const double vgd = polarity * (gate - drain);
    const auto channel = [&](double overdrive, double voltage, double sign) {
        if (overdrive <= 0.0) return std::array<double, 3>{0.0, 0.0, 0.0};
        if (voltage < overdrive) {
            const double factor = 2.0 * overdrive - voltage;
            if (!std::isfinite(factor) || voltage > normalized_limit / std::max(factor, 1.0e-30)) {
                return std::array<double, 3>{sign * model.static_model.max_current, 0.0, 0.0};
            }
            return std::array<double, 3>{sign * parameter * (2.0 * overdrive * voltage - voltage * voltage),
                                         sign * parameter * 2.0 * voltage,
                                         sign * parameter * (2.0 * overdrive - 2.0 * voltage)};
        }
        if (overdrive > std::sqrt(normalized_limit)) {
            return std::array<double, 3>{sign * model.static_model.max_current, 0.0, 0.0};
        }
        return std::array<double, 3>{sign * parameter * overdrive * overdrive,
                                     sign * 2.0 * parameter * overdrive, 0.0};
    };
    double channel_current{};
    double d_gate{};
    double d_drain{};
    if (vds >= 0.0) {
        const auto result = channel(vgs + pinch, vds, 1.0);
        channel_current = result[0]; d_gate = result[1]; d_drain = result[2];
    } else {
        const auto result = channel(vgs - vds + pinch, -vds, -1.0);
        channel_current = result[0]; d_gate = result[1]; d_drain = -result[1] - result[2];
    }
    double modulation = 1.0;
    double modulation_derivative = 0.0;
    const double limited_abs_vds = std::min(std::fabs(vds), 1.0e6);
    if (model.channel_length_modulation > 0.0) {
        modulation = 1.0 + limited_abs_vds * model.channel_length_modulation;
        modulation_derivative = std::fabs(vds) < 1.0e6
            ? model.channel_length_modulation * (vds >= 0.0 ? 1.0 : -1.0) : 0.0;
    }
    channel_current *= modulation;
    d_gate *= modulation;
    d_drain = d_drain * modulation + channel_current / std::max(modulation, 1.0e-30) * modulation_derivative * polarity;
    if (std::fabs(channel_current) >= model.static_model.max_current) {
        channel_current = std::clamp(channel_current, -model.static_model.max_current, model.static_model.max_current);
        d_gate = 0.0; d_drain = 0.0;
    }

    const auto gate_exponential = nonlinear_detail::BoundedExponential(
        (model.gate_leakage_temperature_coefficient * (model.temperature_celsius - kReferenceTemperatureCelsius)));
    const double gate_is = model.gate_saturation_current * gate_exponential.value;
    const double gate_vt = model.gate_thermal_voltage * temperature_ratio;
    const auto gs_exp = nonlinear_detail::BoundedExponential(vgs / gate_vt);
    const auto gd_exp = nonlinear_detail::BoundedExponential(vgd / gate_vt);
    const double igs = gate_is * (gs_exp.value - 1.0);
    const double igd = gate_is * (gd_exp.value - 1.0);
    const double ggs = gate_is / gate_vt * gs_exp.derivative_scale;
    const double ggd = gate_is / gate_vt * gd_exp.derivative_scale;
    NonlinearJfetDynamicEvaluation result{};
    result.currents[0] = polarity * channel_current - polarity * igd;
    result.currents[1] = polarity * (igs + igd);
    result.currents[2] = -polarity * channel_current - polarity * igs;
    const double channel_drain = d_drain;
    const double channel_gate = d_gate;
    const double channel_source = -channel_drain - channel_gate;
    const double gs_g = ggs;
    const double gd_g = ggd;
    result.jacobian[0][0] = channel_drain + gd_g;
    result.jacobian[0][1] = channel_gate - gd_g;
    result.jacobian[0][2] = channel_source;
    result.jacobian[1][0] = -gd_g;
    result.jacobian[1][1] = gs_g + gd_g;
    result.jacobian[1][2] = -gs_g;
    result.jacobian[2][0] = -channel_drain;
    result.jacobian[2][1] = -channel_gate + gs_g;
    result.jacobian[2][2] = -channel_source - gs_g;

    auto companion = [&](double voltage, double previous) {
        const double base_term = std::max(1.0 - voltage / model.gate_junction_potential, 0.05);
        const bool clamped = base_term <= 0.0500000001;
        const double capacitance = model.gate_junction_capacitance * std::pow(base_term, -model.gate_grading_coefficient);
        const double dcap = clamped ? 0.0 : model.gate_junction_capacitance * model.gate_grading_coefficient /
            model.gate_junction_potential * std::pow(base_term, -model.gate_grading_coefficient - 1.0);
        return std::array<double, 2>{capacitance * (voltage - previous) * conductance_scale,
            (capacitance + dcap * (voltage - previous)) * conductance_scale};
    };
    const auto gs = companion(vgs, previous_gs_voltage);
    const auto gd = companion(vgd, previous_gd_voltage);
    auto apply = [&](const std::array<double, 2>& cap, int positive, int negative) {
        result.currents[positive] += polarity * cap[0];
        result.currents[negative] -= polarity * cap[0];
        result.jacobian[positive][positive] += cap[1];
        result.jacobian[positive][negative] -= cap[1];
        result.jacobian[negative][positive] -= cap[1];
        result.jacobian[negative][negative] += cap[1];
    };
    apply(gs, 1, 2);
    apply(gd, 1, 0);
    result.gs_capacitance = model.gate_junction_capacitance * std::pow(std::max(1.0 - vgs / model.gate_junction_potential, 0.05), -model.gate_grading_coefficient);
    result.gd_capacitance = model.gate_junction_capacitance * std::pow(std::max(1.0 - vgd / model.gate_junction_potential, 0.05), -model.gate_grading_coefficient);
    for (const auto current : result.currents) if (!std::isfinite(current)) return {.error = NonlinearDeviceError::NonFinite};
    for (const auto& row : result.jacobian) for (const auto value : row) if (!std::isfinite(value)) return {.error = NonlinearDeviceError::NonFinite};
    result.error = NonlinearDeviceError::None;
    return result;
}
} // namespace solo_rig
