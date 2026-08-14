#include "dsp/dynamic_power_supply.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {
namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
}

bool ValidateDynamicPowerSupplyModel(const DynamicPowerSupplyModel& model) noexcept {
    return std::isfinite(model.voltage) && model.voltage >= 0.0 && model.voltage <= 1.0e6 &&
           std::isfinite(model.source_resistance) && model.source_resistance >= 0.0 && model.source_resistance <= 1.0e9 &&
           std::isfinite(model.sag_resistance) && model.sag_resistance >= 0.0 && model.sag_resistance <= 1.0e9 &&
           std::isfinite(model.recovery_time_seconds) && model.recovery_time_seconds >= 1.0e-9 && model.recovery_time_seconds <= 1.0e6 &&
           std::isfinite(model.ripple_amplitude) && model.ripple_amplitude >= 0.0 && model.ripple_amplitude <= model.voltage &&
           std::isfinite(model.ripple_frequency_hz) && model.ripple_frequency_hz >= 0.0 && model.ripple_frequency_hz <= 1.0e6 &&
           std::isfinite(model.maximum_sag) && model.maximum_sag >= 0.0 && model.maximum_sag <= model.voltage &&
           std::isfinite(model.maximum_load_current) && model.maximum_load_current >= 1.0e-12 &&
           model.maximum_load_current <= 1.0e6;
}

DynamicPowerSupplyEvaluation EvaluateDynamicPowerSupply(
    const DynamicPowerSupplyModel& model,
    double branch_current,
    const DynamicPowerSupplyState& previous_state,
    double timestep_seconds) noexcept {
    DynamicPowerSupplyEvaluation result{};
    if (!ValidateDynamicPowerSupplyModel(model)) {
        result.error = NonlinearDeviceError::InvalidModel;
        return result;
    }
    if (!std::isfinite(branch_current) || !std::isfinite(previous_state.sag) ||
        !std::isfinite(previous_state.ripple_phase) || !std::isfinite(timestep_seconds)) {
        result.error = NonlinearDeviceError::NonFinite;
        return result;
    }
    if (previous_state.sag < 0.0 || previous_state.sag > model.maximum_sag || timestep_seconds <= 0.0) {
        result.error = NonlinearDeviceError::InvalidModel;
        return result;
    }

    double load_current{};
    double load_derivative{};
    if (branch_current < 0.0) {
        if (branch_current <= -model.maximum_load_current) {
            load_current = model.maximum_load_current;
        } else {
            load_current = -branch_current;
            load_derivative = -1.0;
        }
    }
    const double ratio = timestep_seconds / model.recovery_time_seconds;
    const double alpha = std::isfinite(ratio) ? ratio / (1.0 + ratio) : 1.0;
    const double raw_sag = previous_state.sag +
        alpha * (model.sag_resistance * load_current - previous_state.sag);
    double sag_derivative = alpha * model.sag_resistance * load_derivative;
    double sag = raw_sag;
    if (raw_sag <= 0.0) {
        sag = 0.0;
        sag_derivative = 0.0;
    } else if (raw_sag >= model.maximum_sag) {
        sag = model.maximum_sag;
        sag_derivative = 0.0;
    }

    const double phase = std::remainder(previous_state.ripple_phase, kTwoPi);
    const double ripple = 0.5 * model.ripple_amplitude * (1.0 - std::cos(phase));
    const double phase_increment = kTwoPi * model.ripple_frequency_hz * timestep_seconds;
    if (!std::isfinite(sag) || !std::isfinite(ripple) || !std::isfinite(phase_increment)) {
        result.error = NonlinearDeviceError::NonFinite;
        return result;
    }
    result.target_voltage = model.voltage - sag - ripple;
    result.target_voltage_derivative_current = -sag_derivative;
    result.next_state = {.sag = sag, .ripple_phase = std::remainder(phase + phase_increment, kTwoPi)};
    if (!std::isfinite(result.target_voltage) || !std::isfinite(result.next_state.ripple_phase)) {
        result.error = NonlinearDeviceError::NonFinite;
    }
    return result;
}

} // namespace solo_rig
