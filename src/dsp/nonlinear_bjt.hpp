#pragma once

#include "dsp/nonlinear_device_common.hpp"

namespace solo_rig {

enum class BjtPolarity {
    Invalid,
    Npn,
    Pnp,
};

struct NonlinearBjtModel {
    BjtPolarity polarity{BjtPolarity::Invalid};
    double saturation_current{};
    double forward_beta{};
    double reverse_beta{};
    double thermal_voltage{};
    double max_current{};
    double max_step{};
};

struct NonlinearBjtEvaluation {
    NonlinearDeviceError error{NonlinearDeviceError::None};
    double currents[3]{};
    double jacobian[3][3]{};

    [[nodiscard]] bool Succeeded() const noexcept { return error == NonlinearDeviceError::None; }
};

[[nodiscard]] bool ValidateNonlinearBjtModel(const NonlinearBjtModel& model) noexcept;
[[nodiscard]] NonlinearBjtEvaluation EvaluateNonlinearBjt(
    const NonlinearBjtModel& model, double collector, double base, double emitter) noexcept;

} // namespace solo_rig
