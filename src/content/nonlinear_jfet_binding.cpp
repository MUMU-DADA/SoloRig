#include "content/nonlinear_jfet_binding.hpp"

#include "dsp/nonlinear_jfet_dynamic.hpp"

#include <algorithm>
#include <string_view>

namespace solo_rig {
namespace {
const double* Parameter(const ModelAuditRecord& record, std::string_view id) noexcept {
    const auto it = std::find_if(record.parameters.begin(), record.parameters.end(),
                                 [id](const auto& parameter) { return parameter.id == id; });
    return it == record.parameters.end() ? nullptr : &it->default_value;
}
}

bool BindAuditedJfetModel(const ModelAuditRecord& record, CircuitJfetModel& destination) noexcept {
    if (record.kind != ModelKind::Jfet || !record.polarity.has_value() ||
        (*record.polarity != "n-channel" && *record.polarity != "p-channel")) return false;
    const auto pinch = Parameter(record, "pinch-off-voltage");
    const auto transconductance = Parameter(record, "transconductance-parameter");
    const auto max_current = Parameter(record, "max-current");
    const auto max_step = Parameter(record, "max-step");
    if (!pinch || !transconductance || !max_current || !max_step) return false;
    const auto value = [&record](std::string_view id, double fallback) {
        const auto parameter = Parameter(record, id);
        return parameter == nullptr ? fallback : *parameter;
    };
    const bool dynamic = Parameter(record, "channel-length-modulation") != nullptr;
    destination = CircuitJfetModel{
        .kind = dynamic ? "generic-dynamic" : "square-law", .model_version = dynamic ? 2U : 1U,
        .polarity = *record.polarity, .pinch_off_voltage = *pinch,
        .transconductance_parameter = *transconductance, .max_current = *max_current, .max_step = *max_step,
        .channel_length_modulation = value("channel-length-modulation", 0.0),
        .temperature_celsius = value("temperature-celsius", 25.0),
        .gate_saturation_current = value("gate-saturation-current", 0.0),
        .gate_thermal_voltage = value("gate-thermal-voltage", 0.02585),
        .gate_junction_capacitance = value("gate-junction-capacitance", 0.0),
        .gate_junction_potential = value("gate-junction-potential", 0.7),
        .gate_grading_coefficient = value("gate-grading-coefficient", 0.5),
        .gate_leakage_temperature_coefficient = value("gate-leakage-temperature-coefficient", 0.0)};
    return ValidateNonlinearJfetDynamicModel({
        .static_model = {.polarity = destination.polarity == "n-channel" ? JfetPolarity::NChannel : JfetPolarity::PChannel,
                         .pinch_off_voltage = destination.pinch_off_voltage,
                         .transconductance_parameter = destination.transconductance_parameter,
                         .max_current = destination.max_current, .max_step = destination.max_step},
        .channel_length_modulation = destination.channel_length_modulation,
        .temperature_celsius = destination.temperature_celsius,
        .gate_saturation_current = destination.gate_saturation_current,
        .gate_thermal_voltage = destination.gate_thermal_voltage,
        .gate_junction_capacitance = destination.gate_junction_capacitance,
        .gate_junction_potential = destination.gate_junction_potential,
        .gate_grading_coefficient = destination.gate_grading_coefficient,
        .gate_leakage_temperature_coefficient = destination.gate_leakage_temperature_coefficient});
}
} // namespace solo_rig
