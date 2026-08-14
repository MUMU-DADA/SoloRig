#pragma once

#include "dsp/nonlinear_mosfet.hpp"

namespace solo_rig {

// Self-authored generic three-terminal extension. The body is permanently tied
// to source; all electrical quantities use SI units.
struct NonlinearMosfetDynamicModel {
    NonlinearMosfetModel static_model{};
    double channel_length_modulation{};
    double subthreshold_current{};
    double subthreshold_slope_voltage{0.08};
    double temperature_celsius{25.0};
    double body_diode_saturation_current{};
    double body_diode_thermal_voltage{0.02585};
    double gate_junction_capacitance{};
    double gate_junction_potential{0.7};
    double gate_grading_coefficient{0.5};
};

struct NonlinearMosfetDynamicEvaluation {
    NonlinearDeviceError error{NonlinearDeviceError::None};
    double currents[3]{};
    double jacobian[3][3]{};
    double gs_capacitance{};
    double gd_capacitance{};

    [[nodiscard]] bool Succeeded() const noexcept { return error == NonlinearDeviceError::None; }
};

[[nodiscard]] bool ValidateNonlinearMosfetDynamicModel(
    const NonlinearMosfetDynamicModel& model) noexcept;
[[nodiscard]] NonlinearMosfetDynamicEvaluation EvaluateNonlinearMosfetDynamic(
    const NonlinearMosfetDynamicModel& model, double drain, double gate, double source,
    double previous_gs_voltage, double previous_gd_voltage, double conductance_scale) noexcept;

} // namespace solo_rig
