#pragma once

#include "dsp/nonlinear_device_common.hpp"

namespace solo_rig {

struct NonlinearVariMuModel {
    double amplification_factor{};
    double perveance{};
    double remote_cutoff_coefficient{};
    double max_current{};
    double max_step{};
};

struct NonlinearVariMuEvaluation {
    NonlinearDeviceError error{NonlinearDeviceError::None};
    double currents[3]{};
    double jacobian[3][3]{};
    [[nodiscard]] bool Succeeded() const noexcept { return error == NonlinearDeviceError::None; }
};

[[nodiscard]] bool ValidateNonlinearVariMuModel(const NonlinearVariMuModel& model) noexcept;
[[nodiscard]] NonlinearVariMuEvaluation EvaluateNonlinearVariMu(
    const NonlinearVariMuModel& model, double plate, double grid, double cathode) noexcept;

} // namespace solo_rig
