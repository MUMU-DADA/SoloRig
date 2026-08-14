#include "content/nonlinear_diode_binding.hpp"

#include "dsp/nonlinear_diode_dynamic.hpp"

#include <algorithm>
#include <string_view>

namespace solo_rig {

namespace {

const double* Parameter(const ModelAuditRecord& record, std::string_view id) noexcept {
    const auto iterator = std::find_if(record.parameters.begin(), record.parameters.end(),
                                       [id](const auto& item) { return item.id == id; });
    return iterator == record.parameters.end() ? nullptr : &iterator->default_value;
}

} // namespace

bool BindAuditedDiodeModel(const ModelAuditRecord& record,
                           CircuitDiodeModel& destination) noexcept {
    if (record.kind != ModelKind::Diode && record.kind != ModelKind::Zener) {
        return false;
    }
    const auto saturation = Parameter(record, "saturation-current");
    const auto thermal = Parameter(record, "thermal-voltage");
    const auto max_step = Parameter(record, "max-step");
    if (saturation == nullptr || thermal == nullptr || max_step == nullptr) {
        return false;
    }
    const bool zener = record.kind == ModelKind::Zener;
    const auto breakdown_voltage = Parameter(record, "breakdown-voltage");
    const auto breakdown_current = Parameter(record, "breakdown-current");
    const auto breakdown_slope = Parameter(record, "breakdown-slope");
    if (zener && (breakdown_voltage == nullptr || breakdown_current == nullptr || breakdown_slope == nullptr)) {
        return false;
    }
    const auto value_or = [&record](std::string_view id, double fallback) {
        const auto value = Parameter(record, id);
        return value == nullptr ? fallback : *value;
    };
    CircuitDiodeModel candidate{
        .kind = zener ? "zener" : "shockley",
        .model_version = 1U,
        .saturation_current = *saturation,
        .thermal_voltage = *thermal,
        .breakdown_voltage = breakdown_voltage == nullptr ? 0.0 : *breakdown_voltage,
        .breakdown_current = breakdown_current == nullptr ? 0.0 : *breakdown_current,
        .breakdown_slope = breakdown_slope == nullptr ? 0.0 : *breakdown_slope,
        .max_step = *max_step,
        .junction_capacitance = value_or("junction-capacitance", 0.0),
        .junction_potential = value_or("junction-potential", 0.7),
        .grading_coefficient = value_or("grading-coefficient", 0.5),
        .temperature_celsius = value_or("temperature-celsius", 25.0),
        .leakage_current = value_or("leakage-current", 0.0),
        .leakage_temperature_coefficient = value_or("leakage-temperature-coefficient", 0.0)};
    const NonlinearDeviceModel static_model{
        .kind = zener ? NonlinearDeviceKind::Zener : NonlinearDeviceKind::Diode,
        .saturation_current = candidate.saturation_current,
        .thermal_voltage = candidate.thermal_voltage,
        .breakdown_voltage = candidate.breakdown_voltage,
        .breakdown_current = candidate.breakdown_current,
        .breakdown_slope = candidate.breakdown_slope,
        .max_step = candidate.max_step};
    if (!ValidateNonlinearDiodeDynamicModel({
            .static_model = static_model,
            .junction_capacitance = candidate.junction_capacitance,
            .junction_potential = candidate.junction_potential,
            .grading_coefficient = candidate.grading_coefficient,
            .temperature_celsius = candidate.temperature_celsius,
            .leakage_current = candidate.leakage_current,
            .leakage_temperature_coefficient = candidate.leakage_temperature_coefficient})) {
        return false;
    }
    destination = candidate;
    return true;
}

} // namespace solo_rig
