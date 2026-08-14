#include "content/nonlinear_mosfet_binding.hpp"

#include "dsp/nonlinear_mosfet_dynamic.hpp"

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

bool BindAuditedMosfetModel(const ModelAuditRecord& record, CircuitMosfetModel& destination) noexcept {
    if (record.kind != ModelKind::Mosfet || !record.polarity.has_value() ||
        (*record.polarity != "n-channel" && *record.polarity != "p-channel")) return false;
    const auto threshold = Parameter(record, "threshold-voltage");
    const auto transconductance = Parameter(record, "transconductance-parameter");
    const auto max_current = Parameter(record, "max-current");
    const auto max_step = Parameter(record, "max-step");
    if (!threshold || !transconductance || !max_current || !max_step) return false;
    const auto value = [&record](std::string_view id, double fallback) {
        const auto parameter = Parameter(record, id);
        return parameter == nullptr ? fallback : *parameter;
    };
    const bool dynamic = Parameter(record, "channel-length-modulation") != nullptr;
    destination = CircuitMosfetModel{
        .kind = dynamic ? "generic-dynamic" : "enhancement-square-law", .model_version = dynamic ? 2U : 1U,
        .polarity = *record.polarity, .threshold_voltage = *threshold,
        .transconductance_parameter = *transconductance, .max_current = *max_current, .max_step = *max_step,
        .channel_length_modulation = value("channel-length-modulation", 0.0),
        .subthreshold_current = value("subthreshold-current", 0.0),
        .subthreshold_slope_voltage = value("subthreshold-slope-voltage", 0.08),
        .temperature_celsius = value("temperature-celsius", 25.0),
        .body_diode_saturation_current = value("body-diode-saturation-current", 0.0),
        .body_diode_thermal_voltage = value("body-diode-thermal-voltage", 0.02585),
        .gate_junction_capacitance = value("gate-junction-capacitance", 0.0),
        .gate_junction_potential = value("gate-junction-potential", 0.7),
        .gate_grading_coefficient = value("gate-grading-coefficient", 0.5)};
    const NonlinearMosfetModel static_model{
        .polarity = destination.polarity == "n-channel" ? MosfetPolarity::NChannel : MosfetPolarity::PChannel,
        .threshold_voltage = destination.threshold_voltage,
        .transconductance_parameter = destination.transconductance_parameter,
        .max_current = destination.max_current, .max_step = destination.max_step};
    if (!dynamic) return ValidateNonlinearMosfetModel(static_model);
    return ValidateNonlinearMosfetDynamicModel({
        .static_model = static_model,
        .channel_length_modulation = destination.channel_length_modulation,
        .subthreshold_current = destination.subthreshold_current,
        .subthreshold_slope_voltage = destination.subthreshold_slope_voltage,
        .temperature_celsius = destination.temperature_celsius,
        .body_diode_saturation_current = destination.body_diode_saturation_current,
        .body_diode_thermal_voltage = destination.body_diode_thermal_voltage,
        .gate_junction_capacitance = destination.gate_junction_capacitance,
        .gate_junction_potential = destination.gate_junction_potential,
        .gate_grading_coefficient = destination.gate_grading_coefficient});
}
} // namespace solo_rig
