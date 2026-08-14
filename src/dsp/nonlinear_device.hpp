#pragma once

namespace solo_rig {

enum class NonlinearDeviceKind {
    Invalid,
    Diode,
    Zener,
    Bjt,
    Jfet,
};

enum class NonlinearDeviceError {
    None,
    InvalidModel,
    NonFinite,
};

enum class BjtPolarity {
    Invalid,
    Npn,
    Pnp,
};

enum class JfetPolarity {
    Invalid,
    NChannel,
    PChannel,
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
[[nodiscard]] bool ValidateNonlinearBjtModel(const NonlinearBjtModel& model) noexcept;
[[nodiscard]] NonlinearBjtEvaluation EvaluateNonlinearBjt(
    const NonlinearBjtModel& model, double collector, double base, double emitter) noexcept;
[[nodiscard]] bool ValidateNonlinearJfetModel(const NonlinearJfetModel& model) noexcept;
[[nodiscard]] NonlinearJfetEvaluation EvaluateNonlinearJfet(
    const NonlinearJfetModel& model, double drain, double gate, double source) noexcept;

} // namespace solo_rig
