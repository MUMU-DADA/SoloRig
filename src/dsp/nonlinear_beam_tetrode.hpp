#pragma once

#include "dsp/nonlinear_device_common.hpp"

namespace solo_rig {

struct NonlinearBeamTetrodeModel {
    double screen_amplification_factor{};
    double perveance{};
    double knee_voltage{};
    double screen_current_fraction{};
    double max_current{};
    double max_step{};
};

struct NonlinearBeamTetrodeEvaluation {
    NonlinearDeviceError error{NonlinearDeviceError::None};
    double currents[4]{};
    double jacobian[4][4]{};
    [[nodiscard]] bool Succeeded() const noexcept { return error == NonlinearDeviceError::None; }
};

[[nodiscard]] bool ValidateNonlinearBeamTetrodeModel(const NonlinearBeamTetrodeModel& model) noexcept;
[[nodiscard]] NonlinearBeamTetrodeEvaluation EvaluateNonlinearBeamTetrode(
    const NonlinearBeamTetrodeModel& model, double plate, double screen, double grid, double cathode) noexcept;

} // namespace solo_rig
