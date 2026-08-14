#include "dsp/nonlinear_device.hpp"

#include <algorithm>
#include <array>
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

bool ValidateNonlinearJfetModel(const NonlinearJfetModel& model) noexcept {
    return (model.polarity == JfetPolarity::NChannel || model.polarity == JfetPolarity::PChannel) &&
           std::isfinite(model.pinch_off_voltage) && model.pinch_off_voltage >= 1.0e-6 &&
           model.pinch_off_voltage <= 100.0 && std::isfinite(model.transconductance_parameter) &&
           model.transconductance_parameter >= 1.0e-12 && model.transconductance_parameter <= 1.0e6 &&
           std::isfinite(model.max_current) && model.max_current >= 1.0e-12 &&
           model.max_current <= 1.0e6 && std::isfinite(model.max_step) &&
           model.max_step >= 1.0e-20 && model.max_step <= 10.0;
}

NonlinearJfetEvaluation EvaluateNonlinearJfet(
    const NonlinearJfetModel& model, double drain, double gate, double source) noexcept {
    if (!ValidateNonlinearJfetModel(model)) {
        return {.error = NonlinearDeviceError::InvalidModel};
    }
    if (!std::isfinite(drain) || !std::isfinite(gate) || !std::isfinite(source)) {
        return {.error = NonlinearDeviceError::NonFinite};
    }

    const double polarity = model.polarity == JfetPolarity::NChannel ? 1.0 : -1.0;
    const double normalized_gate_source = polarity * (gate - source);
    const double normalized_drain_source = polarity * (drain - source);
    const double pinch = model.pinch_off_voltage;
    const double parameter = model.transconductance_parameter;

    double normalized_current = 0.0;
    double derivative_gate = 0.0;
    double derivative_drain = 0.0;
    const double normalized_current_limit = model.max_current / parameter;
    const auto channel = [&](double overdrive, double voltage, double sign) {
        if (overdrive <= 0.0) {
            return std::array<double, 3>{0.0, 0.0, 0.0};
        }
        if (voltage < overdrive) {
            const double factor = 2.0 * overdrive - voltage;
            if (!std::isfinite(factor) || voltage > normalized_current_limit / factor) {
                return std::array<double, 3>{sign * model.max_current, 0.0, 0.0};
            }
            return std::array<double, 3>{
                sign * parameter * (2.0 * overdrive * voltage - voltage * voltage),
                sign * parameter * (2.0 * voltage),
                sign * parameter * (2.0 * overdrive - 2.0 * voltage)};
        }
        if (overdrive > std::sqrt(normalized_current_limit)) {
            return std::array<double, 3>{sign * model.max_current, 0.0, 0.0};
        }
        return std::array<double, 3>{sign * parameter * overdrive * overdrive,
                                     sign * 2.0 * parameter * overdrive, 0.0};
    };
    if (normalized_drain_source >= 0.0) {
        const auto result = channel(normalized_gate_source + pinch, normalized_drain_source, 1.0);
        normalized_current = result[0];
        derivative_gate = result[1];
        derivative_drain = result[2];
    } else {
        const double reverse_gate_source = normalized_gate_source - normalized_drain_source;
        const auto result = channel(reverse_gate_source + pinch, -normalized_drain_source, -1.0);
        normalized_current = result[0];
        // Reverse orientation uses VGD and -VDS; map its derivatives back to VGS/VDS.
        derivative_gate = result[1];
        derivative_drain = -result[1] - result[2];
    }
    if (std::fabs(normalized_current) >= model.max_current) {
        normalized_current = std::clamp(normalized_current, -model.max_current, model.max_current);
        derivative_gate = 0.0;
        derivative_drain = 0.0;
    }

    const double current = polarity * normalized_current;
    const double d_id_drain = derivative_drain;
    const double d_id_gate = derivative_gate;
    const double d_id_source = -d_id_drain - d_id_gate;
    NonlinearJfetEvaluation result{.error = NonlinearDeviceError::None};
    result.currents[0] = current;
    result.currents[1] = 0.0;
    result.currents[2] = -current;
    result.jacobian[0][0] = d_id_drain;
    result.jacobian[0][1] = d_id_gate;
    result.jacobian[0][2] = d_id_source;
    result.jacobian[1][0] = 0.0;
    result.jacobian[1][1] = 0.0;
    result.jacobian[1][2] = 0.0;
    result.jacobian[2][0] = -d_id_drain;
    result.jacobian[2][1] = -d_id_gate;
    result.jacobian[2][2] = -d_id_source;
    for (const auto current_value : result.currents) {
        if (!std::isfinite(current_value)) {
            return {.error = NonlinearDeviceError::NonFinite};
        }
    }
    for (const auto& row : result.jacobian) {
        for (const auto value : row) {
            if (!std::isfinite(value)) {
                return {.error = NonlinearDeviceError::NonFinite};
            }
        }
    }
    return result;
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
