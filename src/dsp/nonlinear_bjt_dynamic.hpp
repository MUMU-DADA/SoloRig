#pragma once

#include "dsp/nonlinear_bjt.hpp"

namespace solo_rig {

struct NonlinearBjtDynamicModel {
    NonlinearBjtModel static_model{};
    double early_voltage{};
    double be_junction_capacitance{};
    double bc_junction_capacitance{};
    double be_junction_potential{0.7};
    double bc_junction_potential{0.7};
    double be_grading_coefficient{0.5};
    double bc_grading_coefficient{0.5};
    double temperature_celsius{25.0};
    double leakage_temperature_coefficient{};
};

struct NonlinearBjtDynamicEvaluation {
    NonlinearDeviceError error{NonlinearDeviceError::None};
    double currents[3]{};
    double jacobian[3][3]{};
    double be_capacitance{};
    double bc_capacitance{};

    [[nodiscard]] bool Succeeded() const noexcept { return error == NonlinearDeviceError::None; }
};

[[nodiscard]] bool ValidateNonlinearBjtDynamicModel(const NonlinearBjtDynamicModel& model) noexcept;
[[nodiscard]] NonlinearBjtDynamicEvaluation EvaluateNonlinearBjtDynamic(
    const NonlinearBjtDynamicModel& model, double collector, double base, double emitter,
    double previous_be_voltage, double previous_bc_voltage, double conductance_scale) noexcept;

} // namespace solo_rig
