#include "dsp/nonlinear_mosfet_dynamic.hpp"

#include "dsp/nonlinear_exponential.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace solo_rig {
namespace {
constexpr double kReferenceKelvin = 298.15;
constexpr double kAbsoluteZeroCelsius = -273.15;
constexpr double kMaximumNormalizedVoltage = 1.0e6;

NonlinearMosfetDynamicEvaluation Error(NonlinearDeviceError error) noexcept {
    return {.error = error};
}
}

bool ValidateNonlinearMosfetDynamicModel(const NonlinearMosfetDynamicModel& model) noexcept {
    return ValidateNonlinearMosfetModel(model.static_model) &&
           std::isfinite(model.channel_length_modulation) && model.channel_length_modulation >= 0.0 &&
           model.channel_length_modulation <= 10.0 && std::isfinite(model.subthreshold_current) &&
           model.subthreshold_current >= 0.0 && model.subthreshold_current <= 1.0 &&
           std::isfinite(model.subthreshold_slope_voltage) && model.subthreshold_slope_voltage >= 1.0e-4 &&
           model.subthreshold_slope_voltage <= 10.0 && std::isfinite(model.temperature_celsius) &&
           model.temperature_celsius >= -100.0 && model.temperature_celsius <= 200.0 &&
           model.temperature_celsius > kAbsoluteZeroCelsius &&
           std::isfinite(model.body_diode_saturation_current) &&
           model.body_diode_saturation_current >= 0.0 && model.body_diode_saturation_current <= 1.0 &&
           std::isfinite(model.body_diode_thermal_voltage) && model.body_diode_thermal_voltage >= 1.0e-6 &&
           model.body_diode_thermal_voltage <= 1.0 && std::isfinite(model.gate_junction_capacitance) &&
           model.gate_junction_capacitance >= 0.0 && model.gate_junction_capacitance <= 1.0e-3 &&
           std::isfinite(model.gate_junction_potential) && model.gate_junction_potential >= 1.0e-3 &&
           model.gate_junction_potential <= 100.0 && std::isfinite(model.gate_grading_coefficient) &&
           model.gate_grading_coefficient >= 0.0 && model.gate_grading_coefficient <= 1.0;
}

NonlinearMosfetDynamicEvaluation EvaluateNonlinearMosfetDynamic(
    const NonlinearMosfetDynamicModel& model, double drain, double gate, double source,
    double previous_gs_voltage, double previous_gd_voltage, double conductance_scale) noexcept {
    if (!ValidateNonlinearMosfetDynamicModel(model)) return Error(NonlinearDeviceError::InvalidModel);
    if (!std::isfinite(drain) || !std::isfinite(gate) || !std::isfinite(source) ||
        !std::isfinite(previous_gs_voltage) || !std::isfinite(previous_gd_voltage) ||
        !std::isfinite(conductance_scale) || conductance_scale <= 0.0) {
        return Error(NonlinearDeviceError::NonFinite);
    }

    const double polarity = model.static_model.polarity == MosfetPolarity::NChannel ? 1.0 : -1.0;
    const double temperature_ratio = (model.temperature_celsius - kAbsoluteZeroCelsius) / kReferenceKelvin;
    const double threshold = model.static_model.threshold_voltage * std::sqrt(temperature_ratio);
    const double parameter = model.static_model.transconductance_parameter / temperature_ratio;
    const double normalized_limit = model.static_model.max_current / std::max(parameter, 1.0e-30);
    const double vgs = polarity * (gate - source);
    const double vds = polarity * (drain - source);
    const double sub_slope = model.subthreshold_slope_voltage * temperature_ratio;

    // The square-law and subthreshold branches share the same orientation and
    // are combined before the finite current clamp, preserving reverse symmetry.
    auto channel = [&](double effective_gate, double positive_vds, double sign) {
        const double overdrive = effective_gate - threshold;
        double current = 0.0;
        double d_gate = 0.0;
        double d_voltage = 0.0;
        if (overdrive > 0.0) {
            if (positive_vds < overdrive) {
                const double factor = 2.0 * overdrive - positive_vds;
                if (std::isfinite(factor) && positive_vds <= normalized_limit / std::max(factor, 1.0e-30)) {
                    current = parameter * (2.0 * overdrive * positive_vds - positive_vds * positive_vds);
                    d_gate = parameter * 2.0 * positive_vds;
                    d_voltage = parameter * (2.0 * overdrive - 2.0 * positive_vds);
                } else {
                    current = model.static_model.max_current;
                }
            } else if (overdrive <= std::sqrt(normalized_limit)) {
                current = parameter * overdrive * overdrive;
                d_gate = 2.0 * parameter * overdrive;
            } else {
                current = model.static_model.max_current;
            }
        }
        const auto exponent = nonlinear_detail::BoundedExponential((effective_gate - threshold) / sub_slope);
        const double sub_amplitude = model.subthreshold_current * exponent.value;
        const double tanh_argument = positive_vds / sub_slope;
        const double bounded_argument = std::clamp(tanh_argument, -50.0, 50.0);
        const double tanh_value = std::tanh(bounded_argument);
        const double sech_squared = 1.0 - tanh_value * tanh_value;
        current += sub_amplitude * tanh_value;
        d_gate += sub_amplitude * exponent.derivative_scale / sub_slope * tanh_value;
        d_voltage += sub_amplitude * sech_squared / sub_slope;
        return std::array<double, 3>{sign * current, sign * d_gate, sign * d_voltage};
    };

    double normalized_current = 0.0;
    double derivative_gate = 0.0;
    double derivative_drain = 0.0;
    if (vds >= 0.0) {
        const auto value = channel(vgs, std::min(vds, kMaximumNormalizedVoltage), 1.0);
        normalized_current = value[0]; derivative_gate = value[1]; derivative_drain = value[2];
    } else {
        const auto value = channel(vgs - vds, std::min(-vds, kMaximumNormalizedVoltage), -1.0);
        normalized_current = value[0]; derivative_gate = value[1];
        derivative_drain = -value[1] - value[2];
    }
    double modulation = 1.0;
    double modulation_derivative = 0.0;
    const double limited_abs_vds = std::min(std::fabs(vds), kMaximumNormalizedVoltage);
    if (model.channel_length_modulation > 0.0) {
        modulation += model.channel_length_modulation * limited_abs_vds;
        if (std::fabs(vds) < kMaximumNormalizedVoltage) {
            modulation_derivative = model.channel_length_modulation * (vds >= 0.0 ? 1.0 : -1.0);
        }
    }
    const double base_channel_current = normalized_current;
    normalized_current *= modulation;
    derivative_gate *= modulation;
    derivative_drain = derivative_drain * modulation + base_channel_current * modulation_derivative;
    if (std::fabs(normalized_current) >= model.static_model.max_current) {
        normalized_current = std::clamp(normalized_current, -model.static_model.max_current,
                                        model.static_model.max_current);
        derivative_gate = 0.0;
        derivative_drain = 0.0;
    }

    const double thermal_voltage = model.body_diode_thermal_voltage * temperature_ratio;
    const auto diode_exponent = nonlinear_detail::BoundedExponential((-vds) / thermal_voltage);
    double body_diode_current = model.body_diode_saturation_current * (diode_exponent.value - 1.0);
    double body_diode_conductance = model.body_diode_saturation_current / thermal_voltage *
        diode_exponent.derivative_scale;
    if (std::fabs(body_diode_current) >= model.static_model.max_current) {
        body_diode_current = std::clamp(body_diode_current, -model.static_model.max_current,
                                        model.static_model.max_current);
        body_diode_conductance = 0.0;
    }

    NonlinearMosfetDynamicEvaluation result{};
    result.currents[0] = polarity * normalized_current - polarity * body_diode_current;
    result.currents[1] = 0.0;
    result.currents[2] = -polarity * normalized_current + polarity * body_diode_current;
    const double channel_drain = derivative_drain;
    const double channel_gate = derivative_gate;
    const double channel_source = -channel_drain - channel_gate;
    result.jacobian[0][0] = channel_drain + body_diode_conductance;
    result.jacobian[0][1] = channel_gate;
    result.jacobian[0][2] = channel_source - body_diode_conductance;
    result.jacobian[1][0] = 0.0;
    result.jacobian[1][1] = 0.0;
    result.jacobian[1][2] = 0.0;
    result.jacobian[2][0] = -channel_drain - body_diode_conductance;
    result.jacobian[2][1] = -channel_gate;
    result.jacobian[2][2] = -channel_source + body_diode_conductance;

    auto companion = [&](double voltage, double previous) {
        const double raw_factor = 1.0 - voltage / model.gate_junction_potential;
        const double factor = std::max(raw_factor, 0.05);
        const double capacitance = model.gate_junction_capacitance *
            std::pow(factor, -model.gate_grading_coefficient);
        const double dcap = raw_factor > 0.05 && model.gate_grading_coefficient > 0.0
            ? model.gate_junction_capacitance * model.gate_grading_coefficient /
                model.gate_junction_potential * std::pow(factor, -model.gate_grading_coefficient - 1.0)
            : 0.0;
        return std::array<double, 2>{capacitance * (voltage - previous) * conductance_scale,
                                     (capacitance + dcap * (voltage - previous)) * conductance_scale};
    };
    const auto gs = companion(vgs, previous_gs_voltage);
    const auto gd = companion(polarity * (gate - drain), previous_gd_voltage);
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
    result.gs_capacitance = model.gate_junction_capacitance *
        std::pow(std::max(1.0 - vgs / model.gate_junction_potential, 0.05),
                 -model.gate_grading_coefficient);
    const double vgd = polarity * (gate - drain);
    result.gd_capacitance = model.gate_junction_capacitance *
        std::pow(std::max(1.0 - vgd / model.gate_junction_potential, 0.05),
                 -model.gate_grading_coefficient);
    for (const double value : result.currents) if (!std::isfinite(value)) return Error(NonlinearDeviceError::NonFinite);
    for (const auto& row : result.jacobian) for (const double value : row)
        if (!std::isfinite(value)) return Error(NonlinearDeviceError::NonFinite);
    result.error = NonlinearDeviceError::None;
    return result;
}
} // namespace solo_rig
