#include "dsp/nonlinear_jfet.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace solo_rig {

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

    double normalized_current = 0.0;
    double derivative_gate = 0.0;
    double derivative_drain = 0.0;
    if (normalized_drain_source >= 0.0) {
        const auto result = channel(normalized_gate_source + pinch, normalized_drain_source, 1.0);
        normalized_current = result[0];
        derivative_gate = result[1];
        derivative_drain = result[2];
    } else {
        const double reverse_gate_source = normalized_gate_source - normalized_drain_source;
        const auto result = channel(reverse_gate_source + pinch, -normalized_drain_source, -1.0);
        normalized_current = result[0];
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

} // namespace solo_rig
