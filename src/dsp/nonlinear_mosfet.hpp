#pragma once

#include "dsp/nonlinear_device_common.hpp"

namespace solo_rig {

enum class MosfetPolarity { Invalid, NChannel, PChannel };

struct NonlinearMosfetModel {
    MosfetPolarity polarity{MosfetPolarity::Invalid};
    double threshold_voltage{};
    double transconductance_parameter{};
    double max_current{};
    double max_step{};
};

struct NonlinearMosfetEvaluation {
    NonlinearDeviceError error{NonlinearDeviceError::None};
    double currents[3]{};
    double jacobian[3][3]{};
    [[nodiscard]] bool Succeeded() const noexcept { return error == NonlinearDeviceError::None; }
};

[[nodiscard]] bool ValidateNonlinearMosfetModel(const NonlinearMosfetModel& model) noexcept;
[[nodiscard]] NonlinearMosfetEvaluation EvaluateNonlinearMosfet(
    const NonlinearMosfetModel& model, double drain, double gate, double source) noexcept;

} // namespace solo_rig
