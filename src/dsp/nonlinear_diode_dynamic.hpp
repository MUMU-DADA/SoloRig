#pragma once

#include "dsp/nonlinear_diode.hpp"

namespace solo_rig {

// Self-authored generic dynamic junction extension. Temperature is in degC;
// electrical quantities use SI units (F, V, A, and 1/K).
struct NonlinearDiodeDynamicModel {
    NonlinearDeviceModel static_model{};
    double junction_capacitance{};
    double junction_potential{0.7};
    double grading_coefficient{0.5};
    double temperature_celsius{25.0};
    double leakage_current{};
    double leakage_temperature_coefficient{};
};

struct NonlinearDiodeDynamicEvaluation {
    NonlinearDeviceError error{NonlinearDeviceError::None};
    double current{};
    double jacobian{};
    double capacitance{};
    double capacitance_derivative{};

    [[nodiscard]] bool Succeeded() const noexcept { return error == NonlinearDeviceError::None; }
};

[[nodiscard]] bool ValidateNonlinearDiodeDynamicModel(
    const NonlinearDiodeDynamicModel& model) noexcept;
[[nodiscard]] NonlinearDiodeDynamicEvaluation EvaluateNonlinearDiodeDynamic(
    const NonlinearDiodeDynamicModel& model, double voltage, double previous_voltage,
    double conductance_scale) noexcept;

} // namespace solo_rig
