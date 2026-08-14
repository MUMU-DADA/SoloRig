#include "dsp/nonlinear_opamp_dynamic.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace solo_rig {
namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;

double SaturatingDifference(double positive, double negative) noexcept {
    constexpr double maximum = std::numeric_limits<double>::max();
    if (negative < 0.0 && positive > maximum + negative) return maximum;
    if (negative > 0.0 && positive < -maximum + negative) return -maximum;
    return positive - negative;
}
}

bool ValidateNonlinearOpAmpDynamicModel(const NonlinearOpAmpDynamicModel& model) noexcept {
    return std::isfinite(model.gain) && model.gain >= 1.0 && model.gain <= 1.0e8 &&
           std::isfinite(model.gain_bandwidth_hz) && model.gain_bandwidth_hz >= 1.0 && model.gain_bandwidth_hz <= 1.0e9 &&
           std::isfinite(model.slew_rate) && model.slew_rate >= 1.0e-3 && model.slew_rate <= 1.0e9 &&
           std::isfinite(model.input_resistance) && model.input_resistance >= 1.0 && model.input_resistance <= 1.0e15 &&
           std::isfinite(model.input_bias_current) && std::fabs(model.input_bias_current) <= 1.0 &&
           std::isfinite(model.input_offset) && std::fabs(model.input_offset) <= 1.0e3 &&
           std::isfinite(model.output_resistance) && model.output_resistance >= 1.0e-3 && model.output_resistance <= 1.0e9 &&
           std::isfinite(model.output_current_limit) && model.output_current_limit >= 1.0e-9 && model.output_current_limit <= 1.0e6 &&
           std::isfinite(model.supply_minimum) && std::isfinite(model.supply_maximum) &&
           model.supply_minimum >= -1.0e6 && model.supply_maximum <= 1.0e6 && model.supply_minimum < model.supply_maximum &&
           std::isfinite(model.minimum_output) && std::isfinite(model.maximum_output) &&
           model.minimum_output >= model.supply_minimum && model.maximum_output <= model.supply_maximum &&
           model.minimum_output < model.maximum_output && std::isfinite(model.max_step) &&
           model.max_step >= 1.0e-20 && model.max_step <= 10.0;
}

NonlinearOpAmpDynamicEvaluation EvaluateNonlinearOpAmpDynamic(
    const NonlinearOpAmpDynamicModel& model,
    double output_positive,
    double output_negative,
    double control_positive,
    double control_negative,
    double previous_state_voltage,
    double timestep_seconds) noexcept {
    NonlinearOpAmpDynamicEvaluation result{};
    if (!ValidateNonlinearOpAmpDynamicModel(model)) {
        result.error = NonlinearDeviceError::InvalidModel;
        return result;
    }
    const double values[6] = {output_positive, output_negative, control_positive,
                              control_negative, previous_state_voltage, timestep_seconds};
    for (const double value : values) {
        if (!std::isfinite(value)) {
            result.error = NonlinearDeviceError::NonFinite;
            return result;
        }
    }
    if (timestep_seconds <= 0.0) {
        result.error = NonlinearDeviceError::InvalidModel;
        return result;
    }

    const double differential = SaturatingDifference(
        SaturatingDifference(control_positive, control_negative), model.input_offset);
    const double target_minimum = std::max(model.minimum_output, model.supply_minimum);
    const double target_maximum = std::min(model.maximum_output, model.supply_maximum);
    const double minimum_input = target_minimum / model.gain;
    const double maximum_input = target_maximum / model.gain;
    double target{};
    double target_derivative = model.gain;
    if (differential <= minimum_input) {
        target = target_minimum;
        target_derivative = 0.0;
    } else if (differential >= maximum_input) {
        target = target_maximum;
        target_derivative = 0.0;
    } else {
        target = model.gain * differential;
    }
    const double pole_product = model.gain_bandwidth_hz * timestep_seconds;
    const double pole_factor = pole_product >= std::numeric_limits<double>::max() / kTwoPi
        ? 1.0 : (kTwoPi * pole_product) / (1.0 + kTwoPi * pole_product);
    double state = previous_state_voltage + pole_factor * (target - previous_state_voltage);
    double state_derivative = pole_factor * target_derivative;
    const double maximum_delta = model.slew_rate * timestep_seconds;
    const double state_delta = state - previous_state_voltage;
    if (state_delta > maximum_delta) {
        state = previous_state_voltage + maximum_delta;
        state_derivative = 0.0;
    } else if (state_delta < -maximum_delta) {
        state = previous_state_voltage - maximum_delta;
        state_derivative = 0.0;
    }
    if (!std::isfinite(state)) {
        result.error = NonlinearDeviceError::NonFinite;
        return result;
    }
    const double output_voltage = SaturatingDifference(output_positive, output_negative);
    const double output_conductance = 1.0 / model.output_resistance;
    const double output_delta = SaturatingDifference(output_voltage, state);
    const double current_limit_voltage = model.output_current_limit * model.output_resistance;
    double output_current{};
    double output_current_derivative = output_conductance;
    if (output_delta >= current_limit_voltage) {
        output_current = model.output_current_limit;
        output_current_derivative = 0.0;
    } else if (output_delta <= -current_limit_voltage) {
        output_current = -model.output_current_limit;
        output_current_derivative = 0.0;
    } else {
        output_current = output_delta * output_conductance;
    }

    result.state_voltage = state;
    result.currents[0] = output_current;
    result.currents[1] = -output_current;
    const double input_current = differential / model.input_resistance + model.input_bias_current;
    if (!std::isfinite(input_current) || !std::isfinite(output_current)) {
        result.error = NonlinearDeviceError::NonFinite;
        return result;
    }
    result.currents[2] = input_current;
    result.currents[3] = -input_current;
    result.jacobian[0][0] = output_current_derivative;
    result.jacobian[0][1] = -output_current_derivative;
    result.jacobian[0][2] = -output_current_derivative * state_derivative;
    result.jacobian[0][3] = -result.jacobian[0][2];
    for (std::size_t column = 0; column < 4U; ++column) {
        result.jacobian[1][column] = -result.jacobian[0][column];
    }
    const double input_conductance = 1.0 / model.input_resistance;
    result.jacobian[2][2] = input_conductance;
    result.jacobian[2][3] = -input_conductance;
    for (std::size_t column = 0; column < 4U; ++column) {
        result.jacobian[3][column] = -result.jacobian[2][column];
    }
    return result;
}

} // namespace solo_rig
