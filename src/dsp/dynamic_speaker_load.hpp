#pragma once

#include "dsp/nonlinear_device_common.hpp"

namespace solo_rig {

struct DynamicSpeakerLoadModel {
    double voice_coil_resistance{};
    double voice_coil_inductance{};
    double force_factor{};
    double moving_mass{};
    double mechanical_resistance{};
    double compliance{};
    double nonlinear_stiffness{};
    double nonlinear_displacement_scale{};
    double maximum_current{};
    double maximum_velocity{};
    double maximum_displacement{};
};

struct DynamicSpeakerLoadState {
    double current{};
    double velocity{};
    double displacement{};
};

struct DynamicSpeakerLoadEvaluation {
    NonlinearDeviceError error{NonlinearDeviceError::None};
    double voltage{};
    double voltage_derivative_current{};
    DynamicSpeakerLoadState next_state{};
    [[nodiscard]] bool Succeeded() const noexcept { return error == NonlinearDeviceError::None; }
};

[[nodiscard]] bool ValidateDynamicSpeakerLoadModel(const DynamicSpeakerLoadModel& model) noexcept;
[[nodiscard]] DynamicSpeakerLoadEvaluation EvaluateDynamicSpeakerLoad(
    const DynamicSpeakerLoadModel& model,
    double branch_current,
    const DynamicSpeakerLoadState& previous_state,
    double timestep_seconds) noexcept;

} // namespace solo_rig
