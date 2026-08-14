#pragma once

#include "dsp/nonlinear_device_common.hpp"

namespace solo_rig {

enum class AmplifierKind { Invalid, OpAmp, Ota };

struct NonlinearAmplifierModel {
    AmplifierKind kind{AmplifierKind::Invalid};
    double gain{};
    double minimum_output{};
    double maximum_output{};
    double max_step{};
};

struct NonlinearAmplifierEvaluation {
    NonlinearDeviceError error{NonlinearDeviceError::None};
    double output{};
    double jacobian[2]{};
    [[nodiscard]] bool Succeeded() const noexcept { return error == NonlinearDeviceError::None; }
};

[[nodiscard]] bool ValidateNonlinearAmplifierModel(const NonlinearAmplifierModel& model) noexcept;
[[nodiscard]] NonlinearAmplifierEvaluation EvaluateNonlinearAmplifier(
    const NonlinearAmplifierModel& model, double control_positive, double control_negative) noexcept;

} // namespace solo_rig
