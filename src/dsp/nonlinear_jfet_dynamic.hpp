#pragma once

#include "dsp/nonlinear_jfet.hpp"

namespace solo_rig {

struct NonlinearJfetDynamicModel {
    NonlinearJfetModel static_model{};
    double channel_length_modulation{};
    double temperature_celsius{25.0};
    double gate_saturation_current{};
    double gate_thermal_voltage{0.02585};
    double gate_junction_capacitance{};
    double gate_junction_potential{0.7};
    double gate_grading_coefficient{0.5};
    double gate_leakage_temperature_coefficient{};
};

struct NonlinearJfetDynamicEvaluation {
    NonlinearDeviceError error{NonlinearDeviceError::None};
    double currents[3]{};
    double jacobian[3][3]{};
    double gs_capacitance{};
    double gd_capacitance{};

    [[nodiscard]] bool Succeeded() const noexcept { return error == NonlinearDeviceError::None; }
};

[[nodiscard]] bool ValidateNonlinearJfetDynamicModel(const NonlinearJfetDynamicModel& model) noexcept;
[[nodiscard]] NonlinearJfetDynamicEvaluation EvaluateNonlinearJfetDynamic(
    const NonlinearJfetDynamicModel& model, double drain, double gate, double source,
    double previous_gs_voltage, double previous_gd_voltage, double conductance_scale) noexcept;

} // namespace solo_rig
