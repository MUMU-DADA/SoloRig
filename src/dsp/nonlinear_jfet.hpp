#pragma once

#include "dsp/nonlinear_device_common.hpp"

namespace solo_rig {

enum class JfetPolarity {
    Invalid,
    NChannel,
    PChannel,
};

struct NonlinearJfetModel {
    JfetPolarity polarity{JfetPolarity::Invalid};
    double pinch_off_voltage{};
    double transconductance_parameter{};
    double max_current{};
    double max_step{};
};

struct NonlinearJfetEvaluation {
    NonlinearDeviceError error{NonlinearDeviceError::None};
    double currents[3]{};
    double jacobian[3][3]{};

    [[nodiscard]] bool Succeeded() const noexcept { return error == NonlinearDeviceError::None; }
};

[[nodiscard]] bool ValidateNonlinearJfetModel(const NonlinearJfetModel& model) noexcept;
[[nodiscard]] NonlinearJfetEvaluation EvaluateNonlinearJfet(
    const NonlinearJfetModel& model, double drain, double gate, double source) noexcept;

} // namespace solo_rig
