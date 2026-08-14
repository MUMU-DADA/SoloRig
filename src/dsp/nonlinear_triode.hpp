#pragma once

#include "dsp/nonlinear_device_common.hpp"

namespace solo_rig {

struct NonlinearTriodeModel {
    double amplification_factor{};
    double perveance{};
    double max_current{};
    double max_step{};
};

struct NonlinearTriodeEvaluation {
    NonlinearDeviceError error{NonlinearDeviceError::None};
    double currents[3]{};
    double jacobian[3][3]{};
    [[nodiscard]] bool Succeeded() const noexcept { return error == NonlinearDeviceError::None; }
};

[[nodiscard]] bool ValidateNonlinearTriodeModel(const NonlinearTriodeModel& model) noexcept;
[[nodiscard]] NonlinearTriodeEvaluation EvaluateNonlinearTriode(
    const NonlinearTriodeModel& model, double plate, double grid, double cathode) noexcept;

} // namespace solo_rig
