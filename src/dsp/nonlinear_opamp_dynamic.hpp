#pragma once

#include "dsp/nonlinear_device_common.hpp"

namespace solo_rig {

struct NonlinearOpAmpDynamicModel {
    double gain{};
    double gain_bandwidth_hz{};
    double slew_rate{};
    double input_resistance{};
    double input_bias_current{};
    double input_offset{};
    double output_resistance{};
    double output_current_limit{};
    double supply_minimum{};
    double supply_maximum{};
    double minimum_output{};
    double maximum_output{};
    double max_step{};
};

struct NonlinearOpAmpDynamicEvaluation {
    NonlinearDeviceError error{NonlinearDeviceError::None};
    double currents[4]{};
    double jacobian[4][4]{};
    double state_voltage{};
    [[nodiscard]] bool Succeeded() const noexcept { return error == NonlinearDeviceError::None; }
};

[[nodiscard]] bool ValidateNonlinearOpAmpDynamicModel(const NonlinearOpAmpDynamicModel& model) noexcept;
[[nodiscard]] NonlinearOpAmpDynamicEvaluation EvaluateNonlinearOpAmpDynamic(
    const NonlinearOpAmpDynamicModel& model,
    double output_positive,
    double output_negative,
    double control_positive,
    double control_negative,
    double previous_state_voltage,
    double timestep_seconds) noexcept;

} // namespace solo_rig
