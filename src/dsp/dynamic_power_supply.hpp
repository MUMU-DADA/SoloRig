#pragma once

#include "dsp/nonlinear_device_common.hpp"

namespace solo_rig {

struct DynamicPowerSupplyModel {
    double voltage{};
    double source_resistance{};
    double sag_resistance{};
    double recovery_time_seconds{};
    double ripple_amplitude{};
    double ripple_frequency_hz{};
    double maximum_sag{};
    double maximum_load_current{};
};

struct DynamicPowerSupplyState {
    double sag{};
    double ripple_phase{};
};

struct DynamicPowerSupplyEvaluation {
    NonlinearDeviceError error{NonlinearDeviceError::None};
    double target_voltage{};
    double target_voltage_derivative_current{};
    DynamicPowerSupplyState next_state{};
    [[nodiscard]] bool Succeeded() const noexcept { return error == NonlinearDeviceError::None; }
};

[[nodiscard]] bool ValidateDynamicPowerSupplyModel(const DynamicPowerSupplyModel& model) noexcept;
[[nodiscard]] DynamicPowerSupplyEvaluation EvaluateDynamicPowerSupply(
    const DynamicPowerSupplyModel& model,
    double branch_current,
    const DynamicPowerSupplyState& previous_state,
    double timestep_seconds) noexcept;

} // namespace solo_rig
