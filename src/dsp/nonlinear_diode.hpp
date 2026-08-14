#pragma once

#include "dsp/nonlinear_device_common.hpp"

namespace solo_rig {

struct NonlinearDeviceModel {
    NonlinearDeviceKind kind{NonlinearDeviceKind::Invalid};
    double saturation_current{};
    double thermal_voltage{};
    double breakdown_voltage{};
    double breakdown_current{};
    double breakdown_slope{};
    double max_step{};
};

struct NonlinearDeviceEvaluation {
    NonlinearDeviceError error{NonlinearDeviceError::None};
    double current{};
    double jacobian{};

    [[nodiscard]] bool Succeeded() const noexcept { return error == NonlinearDeviceError::None; }
};

[[nodiscard]] bool ValidateNonlinearDeviceModel(const NonlinearDeviceModel& model) noexcept;
[[nodiscard]] NonlinearDeviceEvaluation EvaluateNonlinearDevice(
    const NonlinearDeviceModel& model, double voltage) noexcept;

} // namespace solo_rig
