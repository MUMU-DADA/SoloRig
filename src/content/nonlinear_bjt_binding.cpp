#include "content/nonlinear_bjt_binding.hpp"

#include "dsp/nonlinear_bjt_dynamic.hpp"

#include <algorithm>
#include <string_view>

namespace solo_rig {
namespace {
const double* Parameter(const ModelAuditRecord& record, std::string_view id) noexcept {
    const auto it = std::find_if(record.parameters.begin(), record.parameters.end(),
                                 [id](const auto& p) { return p.id == id; });
    return it == record.parameters.end() ? nullptr : &it->default_value;
}
}

bool BindAuditedBjtModel(const ModelAuditRecord& record, CircuitBjtModel& destination) noexcept {
    if (record.kind != ModelKind::Bjt || !record.polarity.has_value() ||
        (*record.polarity != "npn" && *record.polarity != "pnp")) return false;
    const auto value = [&](std::string_view id) { return Parameter(record, id); };
    const auto saturation = value("saturation-current");
    const auto forward = value("forward-beta");
    const auto reverse = value("reverse-beta");
    const auto thermal = value("thermal-voltage");
    const auto max_current = value("max-current");
    const auto max_step = value("max-step");
    if (!saturation || !forward || !reverse || !thermal || !max_current || !max_step) return false;
    destination = CircuitBjtModel{.kind = Parameter(record, "early-voltage") ? "generic-dynamic" : "ebers-moll",
        .model_version = Parameter(record, "early-voltage") ? 2U : 1U, .polarity = *record.polarity,
        .saturation_current = *saturation, .forward_beta = *forward, .reverse_beta = *reverse,
        .thermal_voltage = *thermal, .max_current = *max_current, .max_step = *max_step,
        .early_voltage = Parameter(record, "early-voltage") ? *Parameter(record, "early-voltage") : 0.0,
        .be_junction_capacitance = Parameter(record, "be-junction-capacitance") ? *Parameter(record, "be-junction-capacitance") : 0.0,
        .bc_junction_capacitance = Parameter(record, "bc-junction-capacitance") ? *Parameter(record, "bc-junction-capacitance") : 0.0,
        .be_junction_potential = Parameter(record, "be-junction-potential") ? *Parameter(record, "be-junction-potential") : 0.7,
        .bc_junction_potential = Parameter(record, "bc-junction-potential") ? *Parameter(record, "bc-junction-potential") : 0.7,
        .be_grading_coefficient = Parameter(record, "be-grading-coefficient") ? *Parameter(record, "be-grading-coefficient") : 0.5,
        .bc_grading_coefficient = Parameter(record, "bc-grading-coefficient") ? *Parameter(record, "bc-grading-coefficient") : 0.5,
        .temperature_celsius = Parameter(record, "temperature-celsius") ? *Parameter(record, "temperature-celsius") : 25.0,
        .leakage_temperature_coefficient = Parameter(record, "leakage-temperature-coefficient") ? *Parameter(record, "leakage-temperature-coefficient") : 0.0};
    return ValidateNonlinearBjtDynamicModel({
        .static_model = {.polarity = destination.polarity == "npn" ? BjtPolarity::Npn : BjtPolarity::Pnp,
                         .saturation_current = destination.saturation_current, .forward_beta = destination.forward_beta,
                         .reverse_beta = destination.reverse_beta, .thermal_voltage = destination.thermal_voltage,
                         .max_current = destination.max_current, .max_step = destination.max_step},
        .early_voltage = destination.early_voltage, .be_junction_capacitance = destination.be_junction_capacitance,
        .bc_junction_capacitance = destination.bc_junction_capacitance, .be_junction_potential = destination.be_junction_potential,
        .bc_junction_potential = destination.bc_junction_potential, .be_grading_coefficient = destination.be_grading_coefficient,
        .bc_grading_coefficient = destination.bc_grading_coefficient, .temperature_celsius = destination.temperature_celsius,
        .leakage_temperature_coefficient = destination.leakage_temperature_coefficient});
}
} // namespace solo_rig
