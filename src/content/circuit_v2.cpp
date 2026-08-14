#include "content/circuit_v2.hpp"

#include "content/json.hpp"
#include "dsp/nonlinear_transformer_core.hpp"
#include "dsp/coupled_inductor.hpp"
#include "dsp/nonlinear_diode_dynamic.hpp"
#include "dsp/dynamic_power_supply.hpp"
#include "dsp/dynamic_speaker_load.hpp"
#include "dsp/dynamic_relay.hpp"
#include "dsp/dynamic_relay_routing.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <unordered_set>

namespace solo_rig {

namespace {

const JsonValue* Find(const JsonValue::Object& object, std::string_view key) {
    const auto iterator = object.find(key);
    return iterator == object.end() ? nullptr : &iterator->second;
}

std::optional<std::string> StringValue(const JsonValue* value) {
    if (value == nullptr || value->AsString() == nullptr || value->AsString()->empty()) {
        return std::nullopt;
    }
    return *value->AsString();
}

std::optional<double> NumberValue(const JsonValue* value) {
    if (value == nullptr || value->AsNumber() == nullptr || !std::isfinite(*value->AsNumber())) {
        return std::nullopt;
    }
    return *value->AsNumber();
}

std::optional<std::uint32_t> UnsignedValue(const JsonValue* value) {
    const auto number = NumberValue(value);
    if (!number.has_value() || *number < 0.0 || *number > static_cast<double>(std::numeric_limits<std::uint32_t>::max()) ||
        *number != std::floor(*number)) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*number);
}

bool IsSafeModelReference(std::string_view model) {
    const std::filesystem::path candidate{std::string(model)};
    if (candidate.is_absolute() || candidate.has_root_name() || candidate.has_root_directory()) {
        return false;
    }
    for (const auto& component : candidate) {
        if (component == "." || component == "..") {
            return false;
        }
    }
    return true;
}

std::optional<CircuitComponentType> ComponentType(std::string_view text) {
    if (text == "resistor") {
        return CircuitComponentType::Resistor;
    }
    if (text == "capacitor") {
        return CircuitComponentType::Capacitor;
    }
    if (text == "inductor") {
        return CircuitComponentType::Inductor;
    }
    if (text == "voltage-source") {
        return CircuitComponentType::VoltageSource;
    }
    if (text == "diode") {
        return CircuitComponentType::Diode;
    }
    if (text == "potentiometer") {
        return CircuitComponentType::Potentiometer;
    }
    if (text == "vccs") {
        return CircuitComponentType::Vccs;
    }
    if (text == "ideal-op-amp") {
        return CircuitComponentType::IdealOpAmp;
    }
    if (text == "opamp") {
        return CircuitComponentType::OpAmp;
    }
    if (text == "ota") {
        return CircuitComponentType::Ota;
    }
    if (text == "bjt") {
        return CircuitComponentType::Bjt;
    }
    if (text == "jfet") {
        return CircuitComponentType::Jfet;
    }
    if (text == "mosfet") {
        return CircuitComponentType::Mosfet;
    }
    if (text == "triode") {
        return CircuitComponentType::Triode;
    }
    if (text == "pentode") {
        return CircuitComponentType::Pentode;
    }
    if (text == "beam-tetrode") {
        return CircuitComponentType::BeamTetrode;
    }
    if (text == "vari-mu") {
        return CircuitComponentType::VariMu;
    }
    if (text == "coupled-inductor") {
        return CircuitComponentType::CoupledInductor;
    }
    if (text == "speaker-load") {
        return CircuitComponentType::SpeakerLoad;
    }
    if (text == "relay") {
        return CircuitComponentType::Relay;
    }
    if (text == "switch") {
        return CircuitComponentType::Switch;
    }
    return std::nullopt;
}

std::optional<CircuitPortDirection> PortDirection(std::string_view text) {
    if (text == "input") {
        return CircuitPortDirection::Input;
    }
    if (text == "output") {
        return CircuitPortDirection::Output;
    }
    return std::nullopt;
}

std::optional<CircuitParameterBindingProperty> ParameterBindingProperty(std::string_view text) {
    if (text == "potentiometer.position") {
        return CircuitParameterBindingProperty::PotentiometerPosition;
    }
    if (text == "switch.state") {
        return CircuitParameterBindingProperty::SwitchState;
    }
    if (text == "supply.voltage") {
        return CircuitParameterBindingProperty::SupplyVoltage;
    }
    return std::nullopt;
}

std::optional<std::vector<std::string>> StringArray(const JsonValue* value) {
    if (value == nullptr || value->AsArray() == nullptr) {
        return std::nullopt;
    }
    std::vector<std::string> result;
    result.reserve(value->AsArray()->size());
    for (const auto& item : *value->AsArray()) {
        const auto text = StringValue(&item);
        if (!text.has_value()) {
            return std::nullopt;
        }
        result.push_back(*text);
    }
    return result;
}

CircuitLoadResult Failure(CircuitLoadError error) {
    return {.circuit = std::nullopt, .errors = {error}};
}

} // namespace

CircuitLoadResult ParseCircuitV2Json(std::string_view source) {
    const auto parsed = ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        return Failure(CircuitLoadError::InvalidJson);
    }
    const auto& object = *parsed.value->AsObject();

    const auto version = UnsignedValue(Find(object, "version"));
    if (!version.has_value()) {
        return Failure(CircuitLoadError::MissingField);
    }
    if (*version != 2U) {
        return Failure(CircuitLoadError::UnsupportedVersion);
    }
    const auto schema = StringValue(Find(object, "schema"));
    if (!schema.has_value()) {
        return Failure(CircuitLoadError::MissingField);
    }
    if (*schema != "circuit") {
        return Failure(CircuitLoadError::InvalidSchema);
    }
    const auto reference_ground = StringValue(Find(object, "referenceGround"));
    if (!reference_ground.has_value()) {
        return Failure(CircuitLoadError::MissingField);
    }

    const auto* node_value = Find(object, "nodes");
    const auto nodes = StringArray(node_value);
    if (!nodes.has_value() || nodes->empty()) {
        return Failure(CircuitLoadError::MissingField);
    }
    std::unordered_set<std::string> node_ids;
    for (const auto& node : *nodes) {
        if (!node_ids.insert(node).second) {
            return Failure(CircuitLoadError::DuplicateId);
        }
    }
    if (!node_ids.contains(*reference_ground)) {
        return Failure(CircuitLoadError::InvalidNode);
    }

    CircuitNetlist circuit;
    circuit.version = *version;
    circuit.schema = *schema;
    circuit.reference_ground = *reference_ground;
    circuit.nodes = *nodes;

    const auto* ports_value = Find(object, "ports");
    if (ports_value == nullptr || ports_value->AsArray() == nullptr || ports_value->AsArray()->empty()) {
        return Failure(CircuitLoadError::MissingField);
    }
    std::unordered_set<std::string> ids;
    bool has_input = false;
    bool has_output = false;
    for (const auto& port_value : *ports_value->AsArray()) {
        const auto* port = port_value.AsObject();
        if (port == nullptr) {
            return Failure(CircuitLoadError::InvalidField);
        }
        const auto id = StringValue(Find(*port, "id"));
        const auto direction_text = StringValue(Find(*port, "direction"));
        const auto node = StringValue(Find(*port, "node"));
        const auto direction = direction_text.has_value() ? PortDirection(*direction_text) : std::nullopt;
        if (!id.has_value() || !direction.has_value() || !node.has_value()) {
            return Failure(CircuitLoadError::InvalidField);
        }
        if (!ids.insert(*id).second) {
            return Failure(CircuitLoadError::DuplicateId);
        }
        if (!node_ids.contains(*node)) {
            return Failure(CircuitLoadError::InvalidNode);
        }
        has_input = has_input || *direction == CircuitPortDirection::Input;
        has_output = has_output || *direction == CircuitPortDirection::Output;
        circuit.ports.push_back({.id = *id, .direction = *direction, .node = *node});
    }
    if (!has_output || std::count_if(circuit.ports.begin(), circuit.ports.end(), [](const CircuitPort& port) {
            return port.direction == CircuitPortDirection::Input;
        }) > 1) {
        return Failure(CircuitLoadError::InvalidField);
    }
    static_cast<void>(has_input);

    if (const auto* supplies_value = Find(object, "supplies"); supplies_value != nullptr) {
        if (supplies_value->AsArray() == nullptr) {
            return Failure(CircuitLoadError::InvalidField);
        }
        for (const auto& supply_value : *supplies_value->AsArray()) {
            const auto* supply = supply_value.AsObject();
            if (supply == nullptr) {
                return Failure(CircuitLoadError::InvalidField);
            }
            const auto id = StringValue(Find(*supply, "id"));
            const auto positive = StringValue(Find(*supply, "positive"));
            const auto negative = StringValue(Find(*supply, "negative"));
            const auto voltage = NumberValue(Find(*supply, "voltage"));
            if (!id.has_value() || !positive.has_value() || !negative.has_value() || !voltage.has_value()) {
                return Failure(CircuitLoadError::InvalidField);
            }
            if (!ids.insert(*id).second) {
                return Failure(CircuitLoadError::DuplicateId);
            }
            if (!node_ids.contains(*positive) || !node_ids.contains(*negative)) {
                return Failure(CircuitLoadError::InvalidNode);
            }
            const auto kind_value = StringValue(Find(*supply, "kind"));
            const std::string kind = kind_value.value_or("ideal");
            const auto model_version = UnsignedValue(Find(*supply, "modelVersion"));
            const auto number = [supply](std::string_view key) { return NumberValue(Find(*supply, key)); };
            const auto source_resistance = number("sourceResistance");
            const auto sag_resistance = number("sagResistance");
            const auto recovery_time = number("recoveryTime");
            const auto ripple_amplitude = number("rippleAmplitude");
            const auto ripple_frequency = number("rippleFrequency");
            const auto maximum_sag = number("maximumSag");
            const auto maximum_load_current = number("maximumLoadCurrent");
            const auto initial_sag = number("initialSag");
            const bool has_dynamic_field = source_resistance.has_value() || sag_resistance.has_value() ||
                recovery_time.has_value() || ripple_amplitude.has_value() || ripple_frequency.has_value() ||
                maximum_sag.has_value() || maximum_load_current.has_value() || initial_sag.has_value();
            const bool dynamic = kind == "rectified-reservoir";
            if ((kind != "ideal" && !dynamic) ||
                (!dynamic && (has_dynamic_field || (model_version.has_value() && *model_version != 1U))) ||
                (dynamic && (!model_version.has_value() || *model_version != 1U || !source_resistance.has_value() ||
                 !sag_resistance.has_value() || !recovery_time.has_value() || !ripple_amplitude.has_value() ||
                 !ripple_frequency.has_value() || !maximum_sag.has_value() || !maximum_load_current.has_value() ||
                 !initial_sag.has_value()))) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            CircuitSupply parsed_supply{.id = *id, .positive_node = *positive,
                                        .negative_node = *negative, .voltage = *voltage,
                                        .kind = kind, .model_version = model_version.value_or(1U)};
            if (dynamic) {
                parsed_supply.source_resistance = *source_resistance;
                parsed_supply.sag_resistance = *sag_resistance;
                parsed_supply.recovery_time_seconds = *recovery_time;
                parsed_supply.ripple_amplitude = *ripple_amplitude;
                parsed_supply.ripple_frequency_hz = *ripple_frequency;
                parsed_supply.maximum_sag = *maximum_sag;
                parsed_supply.maximum_load_current = *maximum_load_current;
                parsed_supply.initial_sag = *initial_sag;
                if (!ValidateDynamicPowerSupplyModel({
                        .voltage = parsed_supply.voltage,
                        .source_resistance = parsed_supply.source_resistance,
                        .sag_resistance = parsed_supply.sag_resistance,
                        .recovery_time_seconds = parsed_supply.recovery_time_seconds,
                        .ripple_amplitude = parsed_supply.ripple_amplitude,
                        .ripple_frequency_hz = parsed_supply.ripple_frequency_hz,
                        .maximum_sag = parsed_supply.maximum_sag,
                        .maximum_load_current = parsed_supply.maximum_load_current}) ||
                    parsed_supply.initial_sag < 0.0 ||
                    parsed_supply.initial_sag > parsed_supply.maximum_sag) {
                    return Failure(CircuitLoadError::InvalidValue);
                }
            }
            circuit.supplies.push_back(std::move(parsed_supply));
        }
    }

    const auto* components_value = Find(object, "components");
    if (components_value == nullptr || components_value->AsArray() == nullptr || components_value->AsArray()->empty()) {
        return Failure(CircuitLoadError::EmptyNetlist);
    }
    for (const auto& component_value : *components_value->AsArray()) {
        const auto* component = component_value.AsObject();
        if (component == nullptr) {
            return Failure(CircuitLoadError::InvalidField);
        }
        const auto id = StringValue(Find(*component, "id"));
        const auto type_text = StringValue(Find(*component, "type"));
        const auto pins = StringArray(Find(*component, "pins"));
        const auto type = type_text.has_value() ? ComponentType(*type_text) : std::nullopt;
        if (!id.has_value() || !type.has_value() || !pins.has_value()) {
            return type_text.has_value() ? Failure(CircuitLoadError::UnknownComponent)
                                         : Failure(CircuitLoadError::MissingField);
        }
        if (!ids.insert(*id).second) {
            return Failure(CircuitLoadError::DuplicateId);
        }
        const bool is_potentiometer = *type == CircuitComponentType::Potentiometer;
        const bool is_controlled_source = *type == CircuitComponentType::Vccs || *type == CircuitComponentType::IdealOpAmp;
        const bool is_amplifier = *type == CircuitComponentType::OpAmp || *type == CircuitComponentType::Ota;
        const bool is_bjt = *type == CircuitComponentType::Bjt;
        const bool is_jfet = *type == CircuitComponentType::Jfet;
        const bool is_mosfet = *type == CircuitComponentType::Mosfet;
        const bool is_triode = *type == CircuitComponentType::Triode;
        const bool is_pentode = *type == CircuitComponentType::Pentode;
        const bool is_beam_tetrode = *type == CircuitComponentType::BeamTetrode;
        const bool is_vari_mu = *type == CircuitComponentType::VariMu;
        const bool is_coupled_inductor = *type == CircuitComponentType::CoupledInductor;
        const bool is_speaker_load = *type == CircuitComponentType::SpeakerLoad;
        const bool is_relay = *type == CircuitComponentType::Relay;
        if ((Find(*component, "bjtModel") != nullptr && !is_bjt) ||
            (Find(*component, "jfetModel") != nullptr && !is_jfet) ||
            (Find(*component, "mosfetModel") != nullptr && !is_mosfet) ||
            (Find(*component, "amplifierModel") != nullptr && !is_amplifier) ||
            (Find(*component, "triodeModel") != nullptr && !is_triode) ||
            (Find(*component, "pentodeModel") != nullptr && !is_pentode) ||
            (Find(*component, "beamTetrodeModel") != nullptr && !is_beam_tetrode) ||
            (Find(*component, "variMuModel") != nullptr && !is_vari_mu) ||
            (Find(*component, "coupledInductorModel") != nullptr && !is_coupled_inductor) ||
            (Find(*component, "speakerModel") != nullptr && !is_speaker_load) ||
            (Find(*component, "relayModel") != nullptr && !is_relay) ||
            (Find(*component, "primaryInitialCurrent") != nullptr && !is_coupled_inductor) ||
            (Find(*component, "secondaryInitialCurrent") != nullptr && !is_coupled_inductor) ||
            (Find(*component, "controlledSource") != nullptr && !is_controlled_source)) {
            return Failure(CircuitLoadError::InvalidValue);
        }
        if (pins->size() != 2U && !(is_potentiometer && pins->size() == 3U) &&
            !((is_controlled_source || is_amplifier) && pins->size() == 4U) &&
            !((is_bjt || is_jfet || is_mosfet || is_triode || is_vari_mu) && pins->size() == 3U) &&
            !((is_pentode || is_beam_tetrode || is_coupled_inductor) && pins->size() == 4U) &&
            !(is_relay && (pins->size() == 4U || pins->size() == 5U || pins->size() == 8U ||
                           pins->size() == 11U || pins->size() == 14U))) {
            return Failure(CircuitLoadError::InvalidPin);
        }
        for (const auto& pin : *pins) {
            if (!node_ids.contains(pin)) {
                return Failure(CircuitLoadError::InvalidPin);
            }
        }

        CircuitComponent parsed_component{.id = *id, .type = *type, .pins = *pins};
        if (const auto* value = Find(*component, "value"); value != nullptr) {
            parsed_component.value = NumberValue(value);
            if (!parsed_component.value.has_value()) {
                return Failure(CircuitLoadError::InvalidValue);
            }
        }
        if (const auto* tolerance = Find(*component, "tolerance"); tolerance != nullptr) {
            parsed_component.tolerance = NumberValue(tolerance);
            if (!parsed_component.tolerance.has_value() || *parsed_component.tolerance < 0.0) {
                return Failure(CircuitLoadError::InvalidValue);
            }
        }
        if (const auto* initial = Find(*component, "initial"); initial != nullptr) {
            parsed_component.initial = NumberValue(initial);
            if (!parsed_component.initial.has_value()) {
                return Failure(CircuitLoadError::InvalidValue);
            }
        }
        if (const auto* model = Find(*component, "model"); model != nullptr) {
            if (const auto model_text = StringValue(model); model_text.has_value()) {
                parsed_component.model = *model_text;
                if (!IsSafeModelReference(*parsed_component.model)) {
                    return Failure(CircuitLoadError::UnsafeAssetPath);
                }
                if (*type == CircuitComponentType::Diode &&
                    (*model_text == "silicon" || *model_text == "shockley")) {
                    parsed_component.diode_model = CircuitDiodeModel{};
                }
            } else if (const auto* model_object = model->AsObject(); model_object != nullptr) {
                const auto kind = StringValue(Find(*model_object, "kind"));
                const auto model_version = Find(*model_object, "modelVersion") == nullptr
                    ? std::optional<std::uint32_t>(1U) : UnsignedValue(Find(*model_object, "modelVersion"));
                const auto saturation = Find(*model_object, "saturationCurrent") == nullptr
                    ? std::optional<double>(1.0e-9) : NumberValue(Find(*model_object, "saturationCurrent"));
                const auto thermal = Find(*model_object, "thermalVoltage") == nullptr
                    ? std::optional<double>(0.02585) : NumberValue(Find(*model_object, "thermalVoltage"));
                const auto max_step = Find(*model_object, "maxStep") == nullptr
                    ? std::optional<double>(1.0) : NumberValue(Find(*model_object, "maxStep"));
                const auto breakdown_voltage = Find(*model_object, "breakdownVoltage") == nullptr
                    ? std::optional<double>{} : NumberValue(Find(*model_object, "breakdownVoltage"));
                const auto breakdown_current = Find(*model_object, "breakdownCurrent") == nullptr
                    ? std::optional<double>{} : NumberValue(Find(*model_object, "breakdownCurrent"));
                const auto breakdown_slope = Find(*model_object, "breakdownSlope") == nullptr
                    ? std::optional<double>{} : NumberValue(Find(*model_object, "breakdownSlope"));
                const auto junction_capacitance = Find(*model_object, "junctionCapacitance") == nullptr
                    ? std::optional<double>(0.0) : NumberValue(Find(*model_object, "junctionCapacitance"));
                const auto junction_potential = Find(*model_object, "junctionPotential") == nullptr
                    ? std::optional<double>(0.7) : NumberValue(Find(*model_object, "junctionPotential"));
                const auto grading_coefficient = Find(*model_object, "gradingCoefficient") == nullptr
                    ? std::optional<double>(0.5) : NumberValue(Find(*model_object, "gradingCoefficient"));
                const auto temperature_celsius = Find(*model_object, "temperatureCelsius") == nullptr
                    ? std::optional<double>(25.0) : NumberValue(Find(*model_object, "temperatureCelsius"));
                const auto leakage_current = Find(*model_object, "leakageCurrent") == nullptr
                    ? std::optional<double>(0.0) : NumberValue(Find(*model_object, "leakageCurrent"));
                const auto leakage_temperature_coefficient =
                    Find(*model_object, "leakageTemperatureCoefficient") == nullptr
                    ? std::optional<double>(0.0) : NumberValue(Find(*model_object, "leakageTemperatureCoefficient"));
                const bool is_shockley = kind.has_value() && *kind == "shockley";
                const bool is_zener = kind.has_value() && *kind == "zener";
                const bool has_breakdown = breakdown_voltage.has_value() || breakdown_current.has_value() ||
                    breakdown_slope.has_value();
                if (*type != CircuitComponentType::Diode || (!is_shockley && !is_zener) ||
                    !model_version.has_value() || *model_version != 1U || !saturation.has_value() ||
                    !thermal.has_value() || !max_step.has_value() || *saturation <= 0.0 || *saturation > 1.0e-3 ||
                    *thermal < 1.0e-6 || *thermal > 1.0 || *max_step < 1.0e-6 || *max_step > 10.0 ||
                    (is_shockley && has_breakdown) ||
                    (is_zener && (!breakdown_voltage.has_value() || !breakdown_current.has_value() ||
                                  !breakdown_slope.has_value() || *breakdown_voltage < 0.1 ||
                                  *breakdown_voltage > 100.0 || *breakdown_current < 1.0e-12 ||
                                  *breakdown_current > 1.0 || *breakdown_slope < 1.0e-6 ||
                                  *breakdown_slope > 10.0)) ||
                    !junction_capacitance.has_value() || !junction_potential.has_value() ||
                    !grading_coefficient.has_value() || !temperature_celsius.has_value() ||
                    !leakage_current.has_value() || !leakage_temperature_coefficient.has_value() ||
                    !ValidateNonlinearDiodeDynamicModel({
                        .static_model = {.kind = is_zener ? NonlinearDeviceKind::Zener : NonlinearDeviceKind::Diode,
                                         .saturation_current = *saturation, .thermal_voltage = *thermal,
                                         .breakdown_voltage = breakdown_voltage.value_or(0.0),
                                         .breakdown_current = breakdown_current.value_or(0.0),
                                         .breakdown_slope = breakdown_slope.value_or(0.0), .max_step = *max_step},
                        .junction_capacitance = junction_capacitance.value_or(0.0),
                        .junction_potential = junction_potential.value_or(0.7),
                        .grading_coefficient = grading_coefficient.value_or(0.5),
                        .temperature_celsius = temperature_celsius.value_or(25.0),
                        .leakage_current = leakage_current.value_or(0.0),
                        .leakage_temperature_coefficient = leakage_temperature_coefficient.value_or(0.0)})) {
                    return Failure(CircuitLoadError::InvalidValue);
                }
                parsed_component.model = is_shockley ? "inline-shockley" : "inline-zener";
                parsed_component.diode_model = CircuitDiodeModel{.kind = *kind,
                                                                   .model_version = *model_version,
                                                                   .saturation_current = *saturation,
                                                                   .thermal_voltage = *thermal,
                                                                   .breakdown_voltage = breakdown_voltage.value_or(0.0),
                                                                   .breakdown_current = breakdown_current.value_or(0.0),
                                                                   .breakdown_slope = breakdown_slope.value_or(0.0),
                                                                   .max_step = *max_step,
                                                                   .junction_capacitance = *junction_capacitance,
                                                                   .junction_potential = *junction_potential,
                                                                   .grading_coefficient = *grading_coefficient,
                                                                   .temperature_celsius = *temperature_celsius,
                                                                   .leakage_current = *leakage_current,
                                                                   .leakage_temperature_coefficient =
                                                                       *leakage_temperature_coefficient};
            } else {
                return Failure(CircuitLoadError::InvalidValue);
            }
        }
        if ((*type == CircuitComponentType::Resistor || *type == CircuitComponentType::Capacitor ||
             *type == CircuitComponentType::Inductor ||
             *type == CircuitComponentType::VoltageSource || *type == CircuitComponentType::Potentiometer) &&
            !parsed_component.value.has_value()) {
            return Failure(CircuitLoadError::InvalidValue);
        }
        if ((*type == CircuitComponentType::Resistor || *type == CircuitComponentType::Capacitor ||
             *type == CircuitComponentType::Inductor || *type == CircuitComponentType::Potentiometer) &&
            *parsed_component.value <= 0.0) {
            return Failure(CircuitLoadError::InvalidValue);
        }
        if (*type == CircuitComponentType::Inductor && parsed_component.model.has_value()) {
            return Failure(CircuitLoadError::InvalidValue);
        }
        if (*type == CircuitComponentType::Diode && !parsed_component.model.has_value()) {
            return Failure(CircuitLoadError::MissingModel);
        }
        // A safe string model is retained as an unresolved reference. The
        // Prepare-time MNA overload accepting ModelDatabase must authorize and
        // bind it before a runtime can be constructed.

        if (is_controlled_source) {
            const auto* controlled = Find(*component, "controlledSource");
            const auto* controlled_object = controlled == nullptr ? nullptr : controlled->AsObject();
            const auto kind = controlled_object == nullptr ? std::nullopt : StringValue(Find(*controlled_object, "kind"));
            const auto model_version = controlled_object == nullptr
                ? std::nullopt
                : UnsignedValue(Find(*controlled_object, "modelVersion"));
            const auto gain = controlled_object == nullptr ? std::nullopt : NumberValue(Find(*controlled_object, "gain"));
            const auto minimum = controlled_object == nullptr || Find(*controlled_object, "minimum") == nullptr
                ? std::optional<double>{}
                : NumberValue(Find(*controlled_object, "minimum"));
            const auto maximum = controlled_object == nullptr || Find(*controlled_object, "maximum") == nullptr
                ? std::optional<double>{}
                : NumberValue(Find(*controlled_object, "maximum"));
            const auto expected_kind = *type == CircuitComponentType::Vccs ? "vccs" : "ideal-op-amp";
            const bool has_any_limit = minimum.has_value() || maximum.has_value();
            const bool has_complete_limits = minimum.has_value() && maximum.has_value();
            if (controlled_object == nullptr || !kind.has_value() || *kind != expected_kind ||
                !model_version.has_value() || *model_version != 1U || !gain.has_value() ||
                (*type == CircuitComponentType::IdealOpAmp && *gain <= 0.0) ||
                (has_any_limit && !has_complete_limits) ||
                (has_complete_limits && *minimum > *maximum)) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            parsed_component.controlled_source = CircuitControlledSourceSpec{.kind = *kind,
                                                                              .model_version = *model_version,
                                                                              .gain = *gain,
                                                                              .minimum = minimum,
                                                                              .maximum = maximum};
        }

        if (const auto* potentiometer = Find(*component, "potentiometer"); potentiometer != nullptr) {
            const auto* object_value = potentiometer->AsObject();
            const auto position = object_value == nullptr ? std::nullopt : NumberValue(Find(*object_value, "position"));
            const auto minimum = object_value == nullptr ? std::nullopt : NumberValue(Find(*object_value, "minimum"));
            const auto maximum = object_value == nullptr ? std::nullopt : NumberValue(Find(*object_value, "maximum"));
            if (!position.has_value() || !minimum.has_value() || !maximum.has_value() || *minimum > *maximum ||
                *position < *minimum || *position > *maximum) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            std::optional<std::string> wiper_node;
            if (const auto* wiper = Find(*object_value, "wiperNode"); wiper != nullptr) {
                wiper_node = StringValue(wiper);
                if (!wiper_node.has_value() || !node_ids.contains(*wiper_node)) {
                    return Failure(CircuitLoadError::InvalidNode);
                }
            }
            if (pins->size() == 3U) {
                if (wiper_node.has_value() && *wiper_node != pins->at(2)) {
                    return Failure(CircuitLoadError::InvalidPin);
                }
                wiper_node = pins->at(2);
            }
            parsed_component.potentiometer = CircuitPotentiometerSpec{.position = *position,
                                                                       .minimum = *minimum,
                                                                       .maximum = *maximum,
                                                                       .wiper_node = std::move(wiper_node)};
        }
        if (const auto* switch_value = Find(*component, "switch"); switch_value != nullptr) {
            const auto* switch_object = switch_value->AsObject();
            const auto states = switch_object == nullptr ? std::nullopt : StringArray(Find(*switch_object, "states"));
            const auto initial = switch_object == nullptr ? std::nullopt : UnsignedValue(Find(*switch_object, "initial"));
            const auto open_resistance = switch_object == nullptr
                ? std::nullopt : NumberValue(Find(*switch_object, "openResistance"));
            const auto closed_resistance = switch_object == nullptr
                ? std::nullopt : NumberValue(Find(*switch_object, "closedResistance"));
            if (!states.has_value() || states->empty() || !initial.has_value() || *initial >= states->size()) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            if (open_resistance.has_value() != closed_resistance.has_value() ||
                (open_resistance.has_value() &&
                 (*open_resistance <= 0.0 || *open_resistance > 1.0e15 ||
                  *closed_resistance <= 0.0 || *closed_resistance > 1.0e15))) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            std::unordered_set<std::string> state_ids(states->begin(), states->end());
            if (state_ids.size() != states->size()) {
                return Failure(CircuitLoadError::DuplicateId);
            }
            parsed_component.switch_spec = CircuitSwitchSpec{
                .states = *states, .initial_state = *initial,
                .open_resistance = open_resistance.value_or(1.0e9),
                .closed_resistance = closed_resistance.value_or(1.0e-3)};
        }
        if (*type == CircuitComponentType::Potentiometer && !parsed_component.potentiometer.has_value()) {
            return Failure(CircuitLoadError::InvalidValue);
        }
        if (*type == CircuitComponentType::Switch && !parsed_component.switch_spec.has_value()) {
            return Failure(CircuitLoadError::InvalidValue);
        }
        if (is_controlled_source && !parsed_component.controlled_source.has_value()) {
            return Failure(CircuitLoadError::InvalidValue);
        }
        if (is_amplifier) {
            const auto* amplifier_value = Find(*component, "amplifierModel");
            const auto* amplifier = amplifier_value == nullptr ? nullptr : amplifier_value->AsObject();
            const auto kind = amplifier == nullptr ? std::nullopt : StringValue(Find(*amplifier, "kind"));
            const auto model_version = amplifier == nullptr
                ? std::nullopt : UnsignedValue(Find(*amplifier, "modelVersion"));
            const auto gain = amplifier == nullptr ? std::nullopt : NumberValue(Find(*amplifier, "gain"));
            const auto minimum = amplifier == nullptr
                ? std::nullopt : NumberValue(Find(*amplifier, "minimumOutput"));
            const auto maximum = amplifier == nullptr
                ? std::nullopt : NumberValue(Find(*amplifier, "maximumOutput"));
            const auto max_step = amplifier == nullptr
                ? std::nullopt : NumberValue(Find(*amplifier, "maxStep"));
            const bool dynamic_kind = kind.has_value() && *kind == "generic-dynamic";
            const auto dynamic_number = [amplifier](std::string_view key) {
                return amplifier == nullptr ? std::nullopt : NumberValue(Find(*amplifier, key));
            };
            const auto gain_bandwidth = dynamic_number("gainBandwidth");
            const auto slew_rate = dynamic_number("slewRate");
            const auto input_resistance = dynamic_number("inputResistance");
            const auto input_bias = dynamic_number("inputBiasCurrent");
            const auto input_offset = dynamic_number("inputOffset");
            const auto output_resistance = dynamic_number("outputResistance");
            const auto output_limit = dynamic_number("outputCurrentLimit");
            const auto supply_minimum = dynamic_number("supplyMinimum");
            const auto supply_maximum = dynamic_number("supplyMaximum");
            const auto expected_kind = *type == CircuitComponentType::OpAmp ? "bounded-opamp" : "bounded-ota";
            const double minimum_gain = *type == CircuitComponentType::OpAmp ? 1.0 : 1.0e-12;
            const double maximum_gain = *type == CircuitComponentType::OpAmp ? 1.0e12 : 1.0e6;
            if (amplifier == nullptr && !parsed_component.model.has_value()) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            const bool valid_static = !dynamic_kind && kind.has_value() && *kind == expected_kind &&
                model_version.has_value() && *model_version == 1U && gain.has_value() &&
                *gain >= minimum_gain && *gain <= maximum_gain && minimum.has_value() &&
                maximum.has_value() && *minimum >= -1.0e6 && *maximum <= 1.0e6 &&
                *minimum < *maximum && max_step.has_value() && *max_step >= 1.0e-20 &&
                *max_step <= 10.0;
            const bool valid_dynamic = dynamic_kind && *type == CircuitComponentType::OpAmp &&
                model_version.has_value() && *model_version == 2U && gain.has_value() && *gain >= 1.0 && *gain <= 1.0e8 &&
                minimum.has_value() && maximum.has_value() && *minimum < *maximum && max_step.has_value() &&
                *max_step >= 1.0e-20 && *max_step <= 10.0 && gain_bandwidth.has_value() && slew_rate.has_value() &&
                input_resistance.has_value() && input_bias.has_value() && input_offset.has_value() &&
                output_resistance.has_value() && output_limit.has_value() && supply_minimum.has_value() && supply_maximum.has_value() &&
                *gain_bandwidth >= 1.0 && *gain_bandwidth <= 1.0e9 && *slew_rate >= 1.0e-3 && *slew_rate <= 1.0e9 &&
                *input_resistance >= 1.0 && *input_resistance <= 1.0e15 && std::fabs(*input_bias) <= 1.0 &&
                std::fabs(*input_offset) <= 1.0e3 && *output_resistance >= 1.0e-3 && *output_resistance <= 1.0e9 &&
                *output_limit >= 1.0e-9 && *output_limit <= 1.0e6 && *supply_minimum >= -1.0e6 &&
                *supply_maximum <= 1.0e6 && *supply_minimum < *supply_maximum &&
                *minimum >= *supply_minimum && *maximum <= *supply_maximum;
            if (amplifier != nullptr && (!(valid_static || valid_dynamic) ||
                *minimum < -1.0e6 || *maximum > 1.0e6 ||
                parsed_component.value.has_value() ||
                parsed_component.tolerance.has_value() || parsed_component.model.has_value() ||
                parsed_component.diode_model.has_value() || parsed_component.potentiometer.has_value() ||
                parsed_component.controlled_source.has_value() || parsed_component.bjt_model.has_value() ||
                parsed_component.jfet_model.has_value() || parsed_component.mosfet_model.has_value() ||
                parsed_component.switch_spec.has_value())) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            if (amplifier != nullptr) {
                parsed_component.amplifier_model = CircuitAmplifierModel{.kind = *kind,
                                                                          .model_version = *model_version,
                                                                          .gain = *gain,
                                                                          .minimum_output = *minimum,
                                                                          .maximum_output = *maximum,
                                                                          .max_step = *max_step,
                                                                          .gain_bandwidth_hz = gain_bandwidth.value_or(0.0),
                                                                          .slew_rate = slew_rate.value_or(0.0),
                                                                          .input_resistance = input_resistance.value_or(0.0),
                                                                          .input_bias_current = input_bias.value_or(0.0),
                                                                          .input_offset = input_offset.value_or(0.0),
                                                                          .output_resistance = output_resistance.value_or(0.0),
                                                                          .output_current_limit = output_limit.value_or(0.0),
                                                                          .supply_minimum = supply_minimum.value_or(0.0),
                                                                          .supply_maximum = supply_maximum.value_or(0.0)};
            }
        }
        if (is_amplifier && !parsed_component.amplifier_model.has_value() && !parsed_component.model.has_value()) {
            return Failure(CircuitLoadError::InvalidValue);
        }
        if (is_bjt) {
            const auto* bjt_value = Find(*component, "bjtModel");
            const auto* bjt = bjt_value == nullptr ? nullptr : bjt_value->AsObject();
            const auto kind = bjt == nullptr ? std::nullopt : StringValue(Find(*bjt, "kind"));
            const auto model_version = bjt == nullptr ? std::nullopt : UnsignedValue(Find(*bjt, "modelVersion"));
            const auto polarity = bjt == nullptr ? std::nullopt : StringValue(Find(*bjt, "polarity"));
            const auto saturation = bjt == nullptr ? std::nullopt : NumberValue(Find(*bjt, "saturationCurrent"));
            const auto forward_beta = bjt == nullptr ? std::nullopt : NumberValue(Find(*bjt, "forwardBeta"));
            const auto reverse_beta = bjt == nullptr ? std::nullopt : NumberValue(Find(*bjt, "reverseBeta"));
            const auto thermal = bjt == nullptr ? std::nullopt : NumberValue(Find(*bjt, "thermalVoltage"));
            const auto max_current = bjt == nullptr ? std::nullopt : NumberValue(Find(*bjt, "maxCurrent"));
            const auto max_step = bjt == nullptr ? std::nullopt : NumberValue(Find(*bjt, "maxStep"));
            const auto early_voltage = bjt == nullptr ? std::nullopt : NumberValue(Find(*bjt, "earlyVoltage"));
            const auto be_capacitance = bjt == nullptr ? std::nullopt : NumberValue(Find(*bjt, "beJunctionCapacitance"));
            const auto bc_capacitance = bjt == nullptr ? std::nullopt : NumberValue(Find(*bjt, "bcJunctionCapacitance"));
            const auto be_potential = bjt == nullptr ? std::nullopt : NumberValue(Find(*bjt, "beJunctionPotential"));
            const auto bc_potential = bjt == nullptr ? std::nullopt : NumberValue(Find(*bjt, "bcJunctionPotential"));
            const auto be_grading = bjt == nullptr ? std::nullopt : NumberValue(Find(*bjt, "beGradingCoefficient"));
            const auto bc_grading = bjt == nullptr ? std::nullopt : NumberValue(Find(*bjt, "bcGradingCoefficient"));
            const auto temperature = bjt == nullptr ? std::nullopt : NumberValue(Find(*bjt, "temperatureCelsius"));
            const auto leakage_tempco = bjt == nullptr ? std::nullopt : NumberValue(Find(*bjt, "leakageTemperatureCoefficient"));
            const bool dynamic_kind = kind.has_value() && *kind == "generic-dynamic";
            if (bjt == nullptr && !parsed_component.model.has_value()) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            if (bjt != nullptr && (!kind.has_value() || (!dynamic_kind && *kind != "ebers-moll") || !model_version.has_value() ||
                ((dynamic_kind && *model_version != 2U) || (!dynamic_kind && *model_version != 1U)) || !polarity.has_value() || (*polarity != "npn" && *polarity != "pnp") ||
                !saturation.has_value() || *saturation <= 0.0 || !forward_beta.has_value() || *forward_beta <= 0.0 ||
                !reverse_beta.has_value() || *reverse_beta <= 0.0 || !thermal.has_value() || *thermal <= 0.0 ||
                !max_current.has_value() || *max_current <= 0.0 || !max_step.has_value() || *max_step <= 0.0 ||
                *saturation > 1.0 || *forward_beta > 1.0e6 || *reverse_beta > 1.0e6 || *thermal > 1.0 ||
                *max_current > 1.0e6 || *max_step > 10.0 || (dynamic_kind &&
                (!early_voltage.has_value() || !be_capacitance.has_value() || !bc_capacitance.has_value() ||
                 !be_potential.has_value() || !bc_potential.has_value() || !be_grading.has_value() ||
                 !bc_grading.has_value() || !temperature.has_value() || !leakage_tempco.has_value() ||
                 *early_voltage < 0.0 || *early_voltage > 1.0e6 || *be_capacitance < 0.0 || *be_capacitance > 1.0e-3 ||
                 *bc_capacitance < 0.0 || *bc_capacitance > 1.0e-3 || *be_potential < 1.0e-3 || *be_potential > 100.0 ||
                 *bc_potential < 1.0e-3 || *bc_potential > 100.0 || *be_grading < 0.0 || *be_grading > 1.0 ||
                 *bc_grading < 0.0 || *bc_grading > 1.0 || *temperature < -100.0 || *temperature > 200.0 ||
                 *leakage_tempco < -0.1 || *leakage_tempco > 0.1)) || parsed_component.value.has_value() ||
                parsed_component.tolerance.has_value() || parsed_component.model.has_value() ||
                parsed_component.diode_model.has_value() || parsed_component.potentiometer.has_value() ||
                parsed_component.controlled_source.has_value() || parsed_component.switch_spec.has_value())) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            if (bjt != nullptr) {
                parsed_component.bjt_model = CircuitBjtModel{.kind = *kind,
                                                         .model_version = *model_version,
                                                         .polarity = *polarity,
                                                         .saturation_current = *saturation,
                                                         .forward_beta = *forward_beta,
                                                         .reverse_beta = *reverse_beta,
                                                         .thermal_voltage = *thermal,
                                                         .max_current = *max_current,
                                                         .max_step = *max_step,
                                                         .early_voltage = early_voltage.value_or(0.0),
                                                         .be_junction_capacitance = be_capacitance.value_or(0.0),
                                                         .bc_junction_capacitance = bc_capacitance.value_or(0.0),
                                                         .be_junction_potential = be_potential.value_or(0.7),
                                                         .bc_junction_potential = bc_potential.value_or(0.7),
                                                         .be_grading_coefficient = be_grading.value_or(0.5),
                                                         .bc_grading_coefficient = bc_grading.value_or(0.5),
                                                         .temperature_celsius = temperature.value_or(25.0),
                                                         .leakage_temperature_coefficient = leakage_tempco.value_or(0.0)};
            }
        }
        if (is_bjt && !parsed_component.bjt_model.has_value() && !parsed_component.model.has_value()) {
            return Failure(CircuitLoadError::InvalidValue);
        }
        if (is_jfet) {
            const auto* jfet_value = Find(*component, "jfetModel");
            const auto* jfet = jfet_value == nullptr ? nullptr : jfet_value->AsObject();
            const auto kind = jfet == nullptr ? std::nullopt : StringValue(Find(*jfet, "kind"));
            const auto model_version = jfet == nullptr ? std::nullopt : UnsignedValue(Find(*jfet, "modelVersion"));
            const auto polarity = jfet == nullptr ? std::nullopt : StringValue(Find(*jfet, "polarity"));
            const auto pinch_off = jfet == nullptr ? std::nullopt : NumberValue(Find(*jfet, "pinchOffVoltage"));
            const auto transconductance = jfet == nullptr
                ? std::nullopt : NumberValue(Find(*jfet, "transconductanceParameter"));
            const auto max_current = jfet == nullptr ? std::nullopt : NumberValue(Find(*jfet, "maxCurrent"));
            const auto max_step = jfet == nullptr ? std::nullopt : NumberValue(Find(*jfet, "maxStep"));
            const auto modulation = jfet == nullptr ? std::nullopt : NumberValue(Find(*jfet, "channelLengthModulation"));
            const auto temperature = jfet == nullptr ? std::nullopt : NumberValue(Find(*jfet, "temperatureCelsius"));
            const auto gate_saturation = jfet == nullptr ? std::nullopt : NumberValue(Find(*jfet, "gateSaturationCurrent"));
            const auto gate_thermal = jfet == nullptr ? std::nullopt : NumberValue(Find(*jfet, "gateThermalVoltage"));
            const auto gate_capacitance = jfet == nullptr ? std::nullopt : NumberValue(Find(*jfet, "gateJunctionCapacitance"));
            const auto gate_potential = jfet == nullptr ? std::nullopt : NumberValue(Find(*jfet, "gateJunctionPotential"));
            const auto gate_grading = jfet == nullptr ? std::nullopt : NumberValue(Find(*jfet, "gateGradingCoefficient"));
            const auto gate_tempco = jfet == nullptr ? std::nullopt : NumberValue(Find(*jfet, "gateLeakageTemperatureCoefficient"));
            const bool dynamic_kind = kind.has_value() && *kind == "generic-dynamic";
            if (jfet == nullptr && !parsed_component.model.has_value()) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            if (jfet != nullptr && (!kind.has_value() || (!dynamic_kind && *kind != "square-law") || !model_version.has_value() ||
                ((dynamic_kind && *model_version != 2U) || (!dynamic_kind && *model_version != 1U)) || !polarity.has_value() ||
                (*polarity != "n-channel" && *polarity != "p-channel") || !pinch_off.has_value() ||
                *pinch_off < 1.0e-6 || *pinch_off > 100.0 || !transconductance.has_value() ||
                *transconductance < 1.0e-12 || *transconductance > 1.0e6 || !max_current.has_value() ||
                *max_current < 1.0e-12 || *max_current > 1.0e6 || !max_step.has_value() ||
                *max_step < 1.0e-20 || *max_step > 10.0 || parsed_component.value.has_value() ||
                parsed_component.tolerance.has_value() || parsed_component.model.has_value() ||
                parsed_component.diode_model.has_value() || parsed_component.potentiometer.has_value() ||
                parsed_component.controlled_source.has_value() || parsed_component.bjt_model.has_value() ||
                parsed_component.switch_spec.has_value() || (dynamic_kind &&
                (!modulation.has_value() || !temperature.has_value() || !gate_saturation.has_value() ||
                 !gate_thermal.has_value() || !gate_capacitance.has_value() || !gate_potential.has_value() ||
                 !gate_grading.has_value() || !gate_tempco.has_value() || *modulation < 0.0 || *modulation > 10.0 ||
                 *temperature < -100.0 || *temperature > 200.0 || *gate_saturation < 0.0 || *gate_saturation > 1.0 ||
                 *gate_thermal < 1.0e-6 || *gate_thermal > 1.0 || *gate_capacitance < 0.0 || *gate_capacitance > 1.0e-3 ||
                 *gate_potential < 1.0e-3 || *gate_potential > 100.0 || *gate_grading < 0.0 || *gate_grading > 1.0 ||
                 *gate_tempco < -0.1 || *gate_tempco > 0.1)))) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            if (jfet != nullptr) {
                parsed_component.jfet_model = CircuitJfetModel{.kind = *kind,
                                                                .model_version = *model_version,
                                                                .polarity = *polarity,
                                                                .pinch_off_voltage = *pinch_off,
                                                                .transconductance_parameter = *transconductance,
                                                                .max_current = *max_current,
                                                                .max_step = *max_step,
                                                                .channel_length_modulation = modulation.value_or(0.0),
                                                                .temperature_celsius = temperature.value_or(25.0),
                                                                .gate_saturation_current = gate_saturation.value_or(0.0),
                                                                .gate_thermal_voltage = gate_thermal.value_or(0.02585),
                                                                .gate_junction_capacitance = gate_capacitance.value_or(0.0),
                                                                .gate_junction_potential = gate_potential.value_or(0.7),
                                                                .gate_grading_coefficient = gate_grading.value_or(0.5),
                                                                .gate_leakage_temperature_coefficient = gate_tempco.value_or(0.0)};
            }
        }
        if (is_jfet && !parsed_component.jfet_model.has_value() && !parsed_component.model.has_value()) {
            return Failure(CircuitLoadError::InvalidValue);
        }
        if (is_mosfet) {
            const auto* mosfet_value = Find(*component, "mosfetModel");
            const auto* mosfet = mosfet_value == nullptr ? nullptr : mosfet_value->AsObject();
            const auto kind = mosfet == nullptr ? std::nullopt : StringValue(Find(*mosfet, "kind"));
            const auto model_version = mosfet == nullptr ? std::nullopt : UnsignedValue(Find(*mosfet, "modelVersion"));
            const auto polarity = mosfet == nullptr ? std::nullopt : StringValue(Find(*mosfet, "polarity"));
            const auto threshold = mosfet == nullptr ? std::nullopt : NumberValue(Find(*mosfet, "thresholdVoltage"));
            const auto transconductance = mosfet == nullptr
                ? std::nullopt : NumberValue(Find(*mosfet, "transconductanceParameter"));
            const auto max_current = mosfet == nullptr ? std::nullopt : NumberValue(Find(*mosfet, "maxCurrent"));
            const auto max_step = mosfet == nullptr ? std::nullopt : NumberValue(Find(*mosfet, "maxStep"));
            const auto channel_modulation = mosfet == nullptr ? std::nullopt : NumberValue(Find(*mosfet, "channelLengthModulation"));
            const auto subthreshold_current = mosfet == nullptr ? std::nullopt : NumberValue(Find(*mosfet, "subthresholdCurrent"));
            const auto subthreshold_slope = mosfet == nullptr ? std::nullopt : NumberValue(Find(*mosfet, "subthresholdSlopeVoltage"));
            const auto temperature = mosfet == nullptr ? std::nullopt : NumberValue(Find(*mosfet, "temperatureCelsius"));
            const auto body_diode_current = mosfet == nullptr ? std::nullopt : NumberValue(Find(*mosfet, "bodyDiodeSaturationCurrent"));
            const auto body_diode_voltage = mosfet == nullptr ? std::nullopt : NumberValue(Find(*mosfet, "bodyDiodeThermalVoltage"));
            const auto gate_capacitance = mosfet == nullptr ? std::nullopt : NumberValue(Find(*mosfet, "gateJunctionCapacitance"));
            const auto gate_potential = mosfet == nullptr ? std::nullopt : NumberValue(Find(*mosfet, "gateJunctionPotential"));
            const auto gate_grading = mosfet == nullptr ? std::nullopt : NumberValue(Find(*mosfet, "gateGradingCoefficient"));
            const bool dynamic = kind.has_value() && *kind == "generic-dynamic" &&
                model_version.has_value() && *model_version == 2U;
            if (mosfet == nullptr && !parsed_component.model.has_value()) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            if (mosfet != nullptr && ((!dynamic && (!kind.has_value() || *kind != "enhancement-square-law" ||
                !model_version.has_value() || *model_version != 1U)) || !polarity.has_value() ||
                (*polarity != "n-channel" && *polarity != "p-channel") || !threshold.has_value() ||
                *threshold < 1.0e-6 || *threshold > 100.0 || !transconductance.has_value() ||
                *transconductance < 1.0e-12 || *transconductance > 1.0e6 || !max_current.has_value() ||
                *max_current < 1.0e-12 || *max_current > 1.0e6 || !max_step.has_value() ||
                *max_step < 1.0e-20 || *max_step > 10.0 || parsed_component.value.has_value() ||
                parsed_component.tolerance.has_value() || parsed_component.model.has_value() ||
                parsed_component.diode_model.has_value() || parsed_component.potentiometer.has_value() ||
                parsed_component.controlled_source.has_value() || parsed_component.bjt_model.has_value() ||
                parsed_component.jfet_model.has_value() || parsed_component.switch_spec.has_value() ||
                (dynamic && (!channel_modulation.has_value() || *channel_modulation < 0.0 || *channel_modulation > 10.0 ||
                             !subthreshold_current.has_value() || *subthreshold_current < 0.0 || *subthreshold_current > 1.0 ||
                             !subthreshold_slope.has_value() || *subthreshold_slope < 1.0e-4 || *subthreshold_slope > 10.0 ||
                             !temperature.has_value() || *temperature < -100.0 || *temperature > 200.0 ||
                             !body_diode_current.has_value() || *body_diode_current < 0.0 || *body_diode_current > 1.0 ||
                             !body_diode_voltage.has_value() || *body_diode_voltage < 1.0e-6 || *body_diode_voltage > 1.0 ||
                             !gate_capacitance.has_value() || *gate_capacitance < 0.0 || *gate_capacitance > 1.0e-3 ||
                             !gate_potential.has_value() || *gate_potential < 1.0e-3 || *gate_potential > 100.0 ||
                             !gate_grading.has_value() || *gate_grading < 0.0 || *gate_grading > 1.0)))) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            if (mosfet != nullptr) {
                parsed_component.mosfet_model = CircuitMosfetModel{.kind = *kind,
                                                                   .model_version = *model_version,
                                                                   .polarity = *polarity,
                                                                   .threshold_voltage = *threshold,
                                                                   .transconductance_parameter = *transconductance,
                                                                   .max_current = *max_current,
                                                                   .max_step = *max_step,
                                                                   .channel_length_modulation = dynamic ? *channel_modulation : 0.0,
                                                                   .subthreshold_current = dynamic ? *subthreshold_current : 0.0,
                                                                   .subthreshold_slope_voltage = dynamic ? *subthreshold_slope : 0.08,
                                                                   .temperature_celsius = dynamic ? *temperature : 25.0,
                                                                   .body_diode_saturation_current = dynamic ? *body_diode_current : 0.0,
                                                                   .body_diode_thermal_voltage = dynamic ? *body_diode_voltage : 0.02585,
                                                                   .gate_junction_capacitance = dynamic ? *gate_capacitance : 0.0,
                                                                   .gate_junction_potential = dynamic ? *gate_potential : 0.7,
                                                                   .gate_grading_coefficient = dynamic ? *gate_grading : 0.5};
            }
        }
        if (is_mosfet && !parsed_component.mosfet_model.has_value() && !parsed_component.model.has_value()) {
            return Failure(CircuitLoadError::InvalidValue);
        }
        if (is_triode) {
            const auto* triode_value = Find(*component, "triodeModel");
            const auto* triode = triode_value == nullptr ? nullptr : triode_value->AsObject();
            const auto kind = triode == nullptr ? std::nullopt : StringValue(Find(*triode, "kind"));
            const auto model_version = triode == nullptr
                ? std::nullopt : UnsignedValue(Find(*triode, "modelVersion"));
            const auto amplification = triode == nullptr
                ? std::nullopt : NumberValue(Find(*triode, "amplificationFactor"));
            const auto perveance = triode == nullptr ? std::nullopt : NumberValue(Find(*triode, "perveance"));
            const auto max_current = triode == nullptr ? std::nullopt : NumberValue(Find(*triode, "maxCurrent"));
            const auto max_step = triode == nullptr ? std::nullopt : NumberValue(Find(*triode, "maxStep"));
            if (triode == nullptr && !parsed_component.model.has_value()) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            if (triode != nullptr && (!kind.has_value() || *kind != "space-charge" ||
                !model_version.has_value() || *model_version != 1U || !amplification.has_value() ||
                *amplification < 1.0 || *amplification > 1.0e6 || !perveance.has_value() ||
                *perveance < 1.0e-12 || *perveance > 1.0e6 || !max_current.has_value() ||
                *max_current < 1.0e-12 || *max_current > 1.0e6 || !max_step.has_value() ||
                *max_step < 1.0e-20 || *max_step > 10.0 || parsed_component.value.has_value() ||
                parsed_component.tolerance.has_value() || parsed_component.model.has_value() ||
                parsed_component.diode_model.has_value() || parsed_component.potentiometer.has_value() ||
                parsed_component.controlled_source.has_value() || parsed_component.bjt_model.has_value() ||
                parsed_component.jfet_model.has_value() || parsed_component.mosfet_model.has_value() ||
                parsed_component.amplifier_model.has_value() || parsed_component.switch_spec.has_value())) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            if (triode != nullptr) {
                parsed_component.triode_model = CircuitTriodeModel{.kind = *kind,
                                                                    .model_version = *model_version,
                                                                    .amplification_factor = *amplification,
                                                                    .perveance = *perveance,
                                                                    .max_current = *max_current,
                                                                    .max_step = *max_step};
            }
        }
        if (is_triode && !parsed_component.triode_model.has_value() && !parsed_component.model.has_value()) {
            return Failure(CircuitLoadError::InvalidValue);
        }
        if (is_pentode) {
            const auto* value = Find(*component, "pentodeModel");
            const auto* model = value == nullptr ? nullptr : value->AsObject();
            const auto kind = model == nullptr ? std::nullopt : StringValue(Find(*model, "kind"));
            const auto model_version = model == nullptr ? std::nullopt : UnsignedValue(Find(*model, "modelVersion"));
            const auto amplification = model == nullptr ? std::nullopt : NumberValue(Find(*model, "screenAmplificationFactor"));
            const auto perveance = model == nullptr ? std::nullopt : NumberValue(Find(*model, "perveance"));
            const auto knee = model == nullptr ? std::nullopt : NumberValue(Find(*model, "kneeVoltage"));
            const auto fraction = model == nullptr ? std::nullopt : NumberValue(Find(*model, "screenCurrentFraction"));
            const auto max_current = model == nullptr ? std::nullopt : NumberValue(Find(*model, "maxCurrent"));
            const auto max_step = model == nullptr ? std::nullopt : NumberValue(Find(*model, "maxStep"));
            if (model == nullptr && !parsed_component.model.has_value()) return Failure(CircuitLoadError::InvalidValue);
            if (model != nullptr && (!kind.has_value() || *kind != "space-charge" || !model_version.has_value() ||
                *model_version != 1U || !amplification.has_value() || *amplification < 1.0 || *amplification > 1.0e6 ||
                !perveance.has_value() || *perveance < 1.0e-12 || *perveance > 1.0e6 || !knee.has_value() ||
                *knee < 1.0e-6 || *knee > 1.0e6 || !fraction.has_value() || *fraction < 0.0 || *fraction > 0.5 ||
                !max_current.has_value() || *max_current < 1.0e-12 || *max_current > 1.0e6 ||
                !max_step.has_value() || *max_step < 1.0e-20 || *max_step > 10.0 ||
                parsed_component.value.has_value() || parsed_component.tolerance.has_value() ||
                parsed_component.model.has_value() || parsed_component.diode_model.has_value() ||
                parsed_component.potentiometer.has_value() || parsed_component.controlled_source.has_value() ||
                parsed_component.bjt_model.has_value() || parsed_component.jfet_model.has_value() ||
                parsed_component.mosfet_model.has_value() || parsed_component.amplifier_model.has_value() ||
                parsed_component.triode_model.has_value() || parsed_component.switch_spec.has_value())) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            if (model != nullptr) {
                parsed_component.pentode_model = CircuitPentodeModel{
                    .kind = *kind, .model_version = *model_version, .screen_amplification_factor = *amplification,
                    .perveance = *perveance, .knee_voltage = *knee, .screen_current_fraction = *fraction,
                    .max_current = *max_current, .max_step = *max_step};
            }
        }
        if (is_pentode && !parsed_component.pentode_model.has_value() && !parsed_component.model.has_value()) {
            return Failure(CircuitLoadError::InvalidValue);
        }
        if (is_beam_tetrode) {
            const auto* value = Find(*component, "beamTetrodeModel");
            const auto* model = value == nullptr ? nullptr : value->AsObject();
            const auto kind = model == nullptr ? std::nullopt : StringValue(Find(*model, "kind"));
            const auto model_version = model == nullptr ? std::nullopt : UnsignedValue(Find(*model, "modelVersion"));
            const auto amplification = model == nullptr ? std::nullopt : NumberValue(Find(*model, "screenAmplificationFactor"));
            const auto perveance = model == nullptr ? std::nullopt : NumberValue(Find(*model, "perveance"));
            const auto knee = model == nullptr ? std::nullopt : NumberValue(Find(*model, "kneeVoltage"));
            const auto fraction = model == nullptr ? std::nullopt : NumberValue(Find(*model, "screenCurrentFraction"));
            const auto max_current = model == nullptr ? std::nullopt : NumberValue(Find(*model, "maxCurrent"));
            const auto max_step = model == nullptr ? std::nullopt : NumberValue(Find(*model, "maxStep"));
            if (model == nullptr && !parsed_component.model.has_value()) return Failure(CircuitLoadError::InvalidValue);
            if (model != nullptr && (!kind.has_value() || *kind != "beam-capture" || !model_version.has_value() ||
                *model_version != 1U || !amplification.has_value() || *amplification < 1.0 || *amplification > 1.0e6 ||
                !perveance.has_value() || *perveance < 1.0e-12 || *perveance > 1.0e6 || !knee.has_value() ||
                *knee < 1.0e-6 || *knee > 1.0e6 || !fraction.has_value() || *fraction < 0.0 || *fraction > 0.5 ||
                !max_current.has_value() || *max_current < 1.0e-12 || *max_current > 1.0e6 ||
                !max_step.has_value() || *max_step < 1.0e-20 || *max_step > 10.0 ||
                parsed_component.value.has_value() || parsed_component.tolerance.has_value() ||
                parsed_component.model.has_value() || parsed_component.diode_model.has_value() ||
                parsed_component.potentiometer.has_value() || parsed_component.controlled_source.has_value() ||
                parsed_component.bjt_model.has_value() || parsed_component.jfet_model.has_value() ||
                parsed_component.mosfet_model.has_value() || parsed_component.amplifier_model.has_value() ||
                parsed_component.triode_model.has_value() || parsed_component.pentode_model.has_value() ||
                parsed_component.switch_spec.has_value())) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            if (model != nullptr) {
                parsed_component.beam_tetrode_model = CircuitBeamTetrodeModel{
                    .kind = *kind, .model_version = *model_version, .screen_amplification_factor = *amplification,
                    .perveance = *perveance, .knee_voltage = *knee, .screen_current_fraction = *fraction,
                    .max_current = *max_current, .max_step = *max_step};
            }
        }
        if (is_beam_tetrode && !parsed_component.beam_tetrode_model.has_value() &&
            !parsed_component.model.has_value()) {
            return Failure(CircuitLoadError::InvalidValue);
        }
        if (is_vari_mu) {
            const auto* value = Find(*component, "variMuModel");
            const auto* model = value == nullptr ? nullptr : value->AsObject();
            const auto kind = model == nullptr ? std::nullopt : StringValue(Find(*model, "kind"));
            const auto model_version = model == nullptr ? std::nullopt : UnsignedValue(Find(*model, "modelVersion"));
            const auto amplification = model == nullptr
                ? std::nullopt : NumberValue(Find(*model, "amplificationFactor"));
            const auto perveance = model == nullptr ? std::nullopt : NumberValue(Find(*model, "perveance"));
            const auto coefficient = model == nullptr
                ? std::nullopt : NumberValue(Find(*model, "remoteCutoffCoefficient"));
            const auto max_current = model == nullptr ? std::nullopt : NumberValue(Find(*model, "maxCurrent"));
            const auto max_step = model == nullptr ? std::nullopt : NumberValue(Find(*model, "maxStep"));
            if (model == nullptr && !parsed_component.model.has_value()) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            if (model != nullptr && (!kind.has_value() || *kind != "remote-cutoff" ||
                !model_version.has_value() || *model_version != 1U || !amplification.has_value() ||
                *amplification < 1.0 || *amplification > 1.0e6 || !perveance.has_value() ||
                *perveance < 1.0e-12 || *perveance > 1.0e6 || !coefficient.has_value() ||
                *coefficient < 1.0e-12 || *coefficient > 1.0e6 || !max_current.has_value() ||
                *max_current < 1.0e-12 || *max_current > 1.0e6 || !max_step.has_value() ||
                *max_step < 1.0e-20 || *max_step > 10.0 || parsed_component.value.has_value() ||
                parsed_component.tolerance.has_value() || parsed_component.model.has_value() ||
                parsed_component.diode_model.has_value() || parsed_component.potentiometer.has_value() ||
                parsed_component.controlled_source.has_value() || parsed_component.bjt_model.has_value() ||
                parsed_component.jfet_model.has_value() || parsed_component.mosfet_model.has_value() ||
                parsed_component.amplifier_model.has_value() || parsed_component.triode_model.has_value() ||
                parsed_component.pentode_model.has_value() || parsed_component.beam_tetrode_model.has_value() ||
                parsed_component.switch_spec.has_value())) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            if (model != nullptr) {
                parsed_component.vari_mu_model = CircuitVariMuModel{
                    .kind = *kind, .model_version = *model_version,
                    .amplification_factor = *amplification, .perveance = *perveance,
                    .remote_cutoff_coefficient = *coefficient,
                    .max_current = *max_current, .max_step = *max_step};
            }
        }
        if (is_vari_mu && !parsed_component.vari_mu_model.has_value() && !parsed_component.model.has_value()) {
            return Failure(CircuitLoadError::InvalidValue);
        }
        if (is_coupled_inductor) {
            const auto* value = Find(*component, "coupledInductorModel");
            const auto* model = value == nullptr ? nullptr : value->AsObject();
            const auto kind = model == nullptr ? std::nullopt : StringValue(Find(*model, "kind"));
            const auto model_version = model == nullptr ? std::nullopt : UnsignedValue(Find(*model, "modelVersion"));
            const auto turns_ratio = model == nullptr ? std::nullopt : NumberValue(Find(*model, "turnsRatio"));
            const auto magnetizing = model == nullptr
                ? std::nullopt : NumberValue(Find(*model, "magnetizingInductance"));
            const auto primary_leakage = model == nullptr
                ? std::nullopt : NumberValue(Find(*model, "primaryLeakageInductance"));
            const auto secondary_leakage = model == nullptr
                ? std::nullopt : NumberValue(Find(*model, "secondaryLeakageInductance"));
            const auto primary_resistance = model == nullptr
                ? std::nullopt : NumberValue(Find(*model, "primaryResistance"));
            const auto secondary_resistance = model == nullptr
                ? std::nullopt : NumberValue(Find(*model, "secondaryResistance"));
            const auto saturation_flux = model == nullptr
                ? std::nullopt : NumberValue(Find(*model, "saturationFlux"));
            const auto hysteresis_width = model == nullptr
                ? std::nullopt : NumberValue(Find(*model, "hysteresisWidth"));
            const auto hysteresis_rate = model == nullptr
                ? std::nullopt : NumberValue(Find(*model, "hysteresisRate"));
            const auto remanent_flux = model == nullptr
                ? std::nullopt : NumberValue(Find(*model, "remanentFlux"));
            const auto max_flux = model == nullptr
                ? std::nullopt : NumberValue(Find(*model, "maxFlux"));
            const auto max_current = model == nullptr
                ? std::nullopt : NumberValue(Find(*model, "maxCurrent"));
            const auto max_step = model == nullptr
                ? std::nullopt : NumberValue(Find(*model, "maxStep"));
            if (model == nullptr && !parsed_component.model.has_value()) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            if (parsed_component.value.has_value() || parsed_component.tolerance.has_value() ||
                parsed_component.initial.has_value()) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            const bool linear_complete = kind.has_value() && *kind == "lossy-linear" && model_version.has_value() &&
                *model_version == 1U && turns_ratio.has_value() && magnetizing.has_value() &&
                primary_leakage.has_value() && secondary_leakage.has_value() &&
                primary_resistance.has_value() && secondary_resistance.has_value() &&
                !saturation_flux.has_value() && !hysteresis_width.has_value() &&
                !hysteresis_rate.has_value() && !remanent_flux.has_value() &&
                !max_flux.has_value() && !max_current.has_value() && !max_step.has_value();
            const bool nonlinear_complete = kind.has_value() && *kind == "saturating-hysteretic" &&
                model_version.has_value() && *model_version == 1U && turns_ratio.has_value() &&
                magnetizing.has_value() && primary_leakage.has_value() && secondary_leakage.has_value() &&
                primary_resistance.has_value() && secondary_resistance.has_value() &&
                saturation_flux.has_value() && hysteresis_width.has_value() && hysteresis_rate.has_value() &&
                remanent_flux.has_value() && max_flux.has_value() && max_current.has_value() && max_step.has_value();
            const bool complete = linear_complete || nonlinear_complete;
            const CoupledInductorModel runtime_model{
                .turns_ratio = turns_ratio.value_or(0.0),
                .magnetizing_inductance = magnetizing.value_or(0.0),
                .primary_leakage_inductance = primary_leakage.value_or(0.0),
                .secondary_leakage_inductance = secondary_leakage.value_or(0.0),
                .primary_resistance = primary_resistance.value_or(0.0),
                .secondary_resistance = secondary_resistance.value_or(0.0)};
            const NonlinearTransformerCoreModel nonlinear_runtime_model{
                .magnetizing_inductance = magnetizing.value_or(0.0),
                .saturation_flux = saturation_flux.value_or(1.0),
                .hysteresis_width = hysteresis_width.value_or(0.1),
                .hysteresis_rate = hysteresis_rate.value_or(1000.0),
                .remanent_flux = remanent_flux.value_or(0.0),
                .max_flux = max_flux.value_or(2.0),
                .max_current = max_current.value_or(10.0),
                .max_step = max_step.value_or(1.0)};
            if (model != nullptr && (!complete || !ValidateCoupledInductorModel(runtime_model) ||
                (*kind == "saturating-hysteretic" &&
                 !ValidateNonlinearTransformerCoreModel(nonlinear_runtime_model)) ||
                parsed_component.model.has_value() ||
                parsed_component.diode_model.has_value() || parsed_component.potentiometer.has_value() ||
                parsed_component.controlled_source.has_value() || parsed_component.bjt_model.has_value() ||
                parsed_component.jfet_model.has_value() || parsed_component.mosfet_model.has_value() ||
                parsed_component.amplifier_model.has_value() || parsed_component.triode_model.has_value() ||
                parsed_component.pentode_model.has_value() || parsed_component.beam_tetrode_model.has_value() ||
                parsed_component.vari_mu_model.has_value() || parsed_component.switch_spec.has_value())) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            if (const auto* initial = Find(*component, "primaryInitialCurrent"); initial != nullptr) {
                parsed_component.primary_initial_current = NumberValue(initial);
                if (!parsed_component.primary_initial_current.has_value()) {
                    return Failure(CircuitLoadError::InvalidValue);
                }
            }
            if (const auto* initial = Find(*component, "secondaryInitialCurrent"); initial != nullptr) {
                parsed_component.secondary_initial_current = NumberValue(initial);
                if (!parsed_component.secondary_initial_current.has_value()) {
                    return Failure(CircuitLoadError::InvalidValue);
                }
            }
            if (model != nullptr) {
                parsed_component.coupled_inductor_model = CircuitCoupledInductorModel{
                    .kind = *kind,
                    .model_version = *model_version,
                    .turns_ratio = *turns_ratio,
                    .magnetizing_inductance = *magnetizing,
                    .primary_leakage_inductance = *primary_leakage,
                    .secondary_leakage_inductance = *secondary_leakage,
                    .primary_resistance = *primary_resistance,
                    .secondary_resistance = *secondary_resistance,
                    .saturation_flux = saturation_flux.value_or(1.0),
                    .hysteresis_width = hysteresis_width.value_or(0.1),
                    .hysteresis_rate = hysteresis_rate.value_or(1000.0),
                    .remanent_flux = remanent_flux.value_or(0.0),
                    .max_flux = max_flux.value_or(2.0),
                    .max_current = max_current.value_or(10.0),
                    .max_step = max_step.value_or(1.0)};
            }
        }
        if (is_speaker_load) {
            const auto* value = Find(*component, "speakerModel");
            const auto* model = value == nullptr ? nullptr : value->AsObject();
            if (model == nullptr && !parsed_component.model.has_value()) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            if (model != nullptr) {
                const auto kind = StringValue(Find(*model, "kind"));
                const auto speaker_version = UnsignedValue(Find(*model, "modelVersion"));
                const auto number = [model](std::string_view key) {
                    return NumberValue(Find(*model, key));
                };
                const auto re = number("voiceCoilResistance");
                const auto le = number("voiceCoilInductance");
                const auto bl = number("forceFactor");
                const auto mass = number("movingMass");
                const auto damping = number("mechanicalResistance");
                const auto compliance = number("compliance");
                const auto nonlinear = number("nonlinearStiffness");
                const auto scale = number("nonlinearDisplacementScale");
                const auto maximum_current = number("maximumCurrent");
                const auto maximum_velocity = number("maximumVelocity");
                const auto maximum_displacement = number("maximumDisplacement");
                const auto initial_current = number("initialCurrent");
                const auto initial_velocity = number("initialVelocity");
                const auto initial_displacement = number("initialDisplacement");
                if (!kind.has_value() || *kind != "electrodynamic" ||
                    !speaker_version.has_value() || *speaker_version != 1U ||
                    !re.has_value() || !le.has_value() || !bl.has_value() ||
                    !mass.has_value() || !damping.has_value() || !compliance.has_value() ||
                    !nonlinear.has_value() || !scale.has_value() || !maximum_current.has_value() ||
                    !maximum_velocity.has_value() || !maximum_displacement.has_value() ||
                    !initial_current.has_value() || !initial_velocity.has_value() ||
                    !initial_displacement.has_value() || parsed_component.model.has_value() ||
                    parsed_component.value.has_value() || parsed_component.tolerance.has_value() ||
                    parsed_component.initial.has_value()) {
                    return Failure(CircuitLoadError::InvalidValue);
                }
                const DynamicSpeakerLoadModel runtime_model{
                    *re, *le, *bl, *mass, *damping, *compliance, *nonlinear, *scale,
                    *maximum_current, *maximum_velocity, *maximum_displacement};
                if (!ValidateDynamicSpeakerLoadModel(runtime_model) ||
                    std::fabs(*initial_current) > *maximum_current ||
                    std::fabs(*initial_velocity) > *maximum_velocity ||
                    std::fabs(*initial_displacement) > *maximum_displacement) {
                    return Failure(CircuitLoadError::InvalidValue);
                }
                parsed_component.speaker_load_model = {
                    .kind = *kind, .model_version = *speaker_version,
                    .voice_coil_resistance = *re, .voice_coil_inductance = *le,
                    .force_factor = *bl, .moving_mass = *mass,
                    .mechanical_resistance = *damping, .compliance = *compliance,
                    .nonlinear_stiffness = *nonlinear,
                    .nonlinear_displacement_scale = *scale,
                    .maximum_current = *maximum_current,
                    .maximum_velocity = *maximum_velocity,
                    .maximum_displacement = *maximum_displacement,
                    .initial_current = *initial_current,
                    .initial_velocity = *initial_velocity,
                    .initial_displacement = *initial_displacement};
            }
        }
        if (is_relay) {
            const auto* value = Find(*component, "relayModel");
            const auto* model = value == nullptr ? nullptr : value->AsObject();
            const auto string = [model](std::string_view key) {
                return model == nullptr ? std::nullopt : StringValue(Find(*model, key));
            };
            const auto number = [model](std::string_view key) {
                return model == nullptr ? std::nullopt : NumberValue(Find(*model, key));
            };
            const auto kind = string("kind");
            const auto relay_version = model == nullptr ? std::nullopt : UnsignedValue(Find(*model, "modelVersion"));
            const auto coil_resistance = number("coilResistance");
            const auto coil_inductance = number("coilInductance");
            const auto open_resistance = number("openResistance");
            const auto closed_resistance = number("closedResistance");
            const auto pull_in = number("pullInCurrent");
            const auto drop_out = number("dropOutCurrent");
            const auto operate_time = number("operateTime");
            const auto release_time = number("releaseTime");
            const auto bounce_duration = number("bounceDuration");
            const auto bounce_frequency = number("bounceFrequency");
            const auto bounce_amplitude = number("bounceAmplitude");
            const auto maximum_current = number("maximumCoilCurrent");
            const auto initial_state = string("initialState");
            const auto pole_count = model == nullptr ? std::nullopt : UnsignedValue(Find(*model, "poleCount"));
            const auto contact_gap = number("contactGap");
            const bool legacy = kind == "generic-dynamic" && relay_version == 1U;
            const bool routed = kind == "generic-multi-contact" && relay_version == 2U;
            if (model == nullptr || (!legacy && !routed) || !coil_resistance.has_value() ||
                !coil_inductance.has_value() || !open_resistance.has_value() ||
                !closed_resistance.has_value() || !pull_in.has_value() || !drop_out.has_value() ||
                !operate_time.has_value() || !release_time.has_value() || !bounce_duration.has_value() ||
                !bounce_frequency.has_value() || !bounce_amplitude.has_value() ||
                !maximum_current.has_value() || !initial_state.has_value() ||
                (legacy && (*initial_state != "open" && *initial_state != "closed")) ||
                (routed && (*initial_state != "released" && *initial_state != "operated")) ||
                (legacy && (pole_count.has_value() || contact_gap.has_value() || pins->size() != 4U)) ||
                (routed && (!pole_count.has_value() || !contact_gap.has_value() ||
                            pins->size() != 2U + 3U * *pole_count)) ||
                parsed_component.value.has_value() || parsed_component.tolerance.has_value() ||
                parsed_component.initial.has_value() || parsed_component.model.has_value()) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            const DynamicRelayModel runtime_model{*coil_resistance, *coil_inductance, *open_resistance,
                *closed_resistance, *pull_in, *drop_out, *operate_time, *release_time,
                *bounce_duration, *bounce_frequency, *bounce_amplitude, *maximum_current};
            if (!ValidateDynamicRelayModel(runtime_model)) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            const DynamicRelayRoutingModel routing_model{
                .pole_count = pole_count.value_or(1U), .contact_gap = contact_gap.value_or(0.0)};
            if (routed && !ValidateDynamicRelayRoutingModel(routing_model)) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            parsed_component.relay_model = {.kind=*kind,.model_version=*relay_version,
                .coil_resistance=*coil_resistance,.coil_inductance=*coil_inductance,
                .open_resistance=*open_resistance,.closed_resistance=*closed_resistance,
                .pull_in_current=*pull_in,.drop_out_current=*drop_out,
                .operate_time_seconds=*operate_time,.release_time_seconds=*release_time,
                .bounce_duration_seconds=*bounce_duration,.bounce_frequency_hz=*bounce_frequency,
                .bounce_amplitude=*bounce_amplitude,.maximum_coil_current=*maximum_current,
                .initial_energized=legacy ? *initial_state=="closed" : *initial_state=="operated",
                .pole_count=routing_model.pole_count,.contact_gap=routing_model.contact_gap};
        }
        circuit.components.push_back(std::move(parsed_component));
    }

    if (const auto* bindings_value = Find(object, "parameterBindings"); bindings_value != nullptr) {
        const auto* bindings = bindings_value->AsArray();
        if (bindings == nullptr) {
            return Failure(CircuitLoadError::InvalidField);
        }
        std::unordered_set<std::string> parameter_ids;
        std::unordered_set<std::string> targets;
        for (const auto& binding_value : *bindings) {
            const auto* binding = binding_value.AsObject();
            if (binding == nullptr) {
                return Failure(CircuitLoadError::InvalidField);
            }
            const auto model_version = UnsignedValue(Find(*binding, "modelVersion"));
            const auto parameter = StringValue(Find(*binding, "parameter"));
            const auto target = StringValue(Find(*binding, "target"));
            const auto property_text = StringValue(Find(*binding, "property"));
            const auto property = property_text.has_value()
                ? ParameterBindingProperty(*property_text) : std::nullopt;
            const auto smoothing = NumberValue(Find(*binding, "smoothingSeconds"));
            if (!model_version.has_value() || *model_version != 1U || !parameter.has_value() ||
                !target.has_value() || !property.has_value() || !smoothing.has_value() ||
                *smoothing < 0.0 || *smoothing > 1.0e6) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            const std::string target_key = *target + "\n" + *property_text;
            if (!parameter_ids.insert(*parameter).second || !targets.insert(target_key).second) {
                return Failure(CircuitLoadError::DuplicateId);
            }
            const auto component = std::find_if(circuit.components.begin(), circuit.components.end(),
                [&target](const CircuitComponent& item) { return item.id == *target; });
            const auto supply = std::find_if(circuit.supplies.begin(), circuit.supplies.end(),
                [&target](const CircuitSupply& item) { return item.id == *target; });
            const bool matches =
                (*property == CircuitParameterBindingProperty::PotentiometerPosition &&
                 component != circuit.components.end() && component->type == CircuitComponentType::Potentiometer) ||
                (*property == CircuitParameterBindingProperty::SwitchState && component != circuit.components.end() &&
                 component->type == CircuitComponentType::Switch && component->switch_spec.has_value() &&
                 component->switch_spec->states == std::vector<std::string>{"open", "closed"}) ||
                (*property == CircuitParameterBindingProperty::SupplyVoltage && supply != circuit.supplies.end());
            if (!matches) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            circuit.parameter_bindings.push_back({.model_version = *model_version,
                .parameter_id = *parameter, .target_id = *target, .property = *property,
                .smoothing_seconds = *smoothing});
        }
    }

    const auto* solver_value = Find(object, "solver");
    const auto* solver = solver_value == nullptr ? nullptr : solver_value->AsObject();
    if (solver == nullptr) {
        return Failure(CircuitLoadError::MissingField);
    }
    const auto method = StringValue(Find(*solver, "method"));
    const auto integration = StringValue(Find(*solver, "integration"));
    if (!method.has_value() || !integration.has_value() || *method != "modified-nodal-analysis" ||
        *integration != "backward-euler") {
        return Failure(CircuitLoadError::InvalidSolver);
    }
    circuit.solver.method = *method;
    circuit.solver.integration = *integration;
    if (const auto* oversampling = Find(*solver, "oversampling"); oversampling != nullptr) {
        const auto value = UnsignedValue(oversampling);
        if (!value.has_value() || *value == 0U || *value > 16U) {
            return Failure(CircuitLoadError::InvalidOversampling);
        }
        circuit.solver.oversampling = *value;
    }
    if (const auto* convergence_value = Find(*solver, "convergence"); convergence_value != nullptr) {
        const auto* convergence = convergence_value->AsObject();
        if (convergence == nullptr) {
            return Failure(CircuitLoadError::InvalidConvergence);
        }
        const auto max_iterations = UnsignedValue(Find(*convergence, "maxIterations"));
        const auto absolute_tolerance = NumberValue(Find(*convergence, "absoluteTolerance"));
        const auto relative_tolerance = NumberValue(Find(*convergence, "relativeTolerance"));
        if (!max_iterations.has_value() || *max_iterations == 0U || *max_iterations > 1024U ||
            !absolute_tolerance.has_value() || *absolute_tolerance <= 0.0 || !relative_tolerance.has_value() ||
            *relative_tolerance <= 0.0) {
            return Failure(CircuitLoadError::InvalidConvergence);
        }
        circuit.solver.convergence = CircuitConvergenceConfig{.max_iterations = *max_iterations,
                                                               .absolute_tolerance = *absolute_tolerance,
                                                               .relative_tolerance = *relative_tolerance};
    }

    if (const auto* baseline_value = Find(object, "measurementBaseline"); baseline_value != nullptr) {
        const auto* baseline = baseline_value->AsObject();
        if (baseline == nullptr) {
            return Failure(CircuitLoadError::InvalidValue);
        }
        CircuitMeasurementBaseline metadata;
        if (const auto* reference = Find(*baseline, "reference"); reference != nullptr) {
            metadata.reference = StringValue(reference);
            if (!metadata.reference.has_value()) {
                return Failure(CircuitLoadError::InvalidValue);
            }
        }
        if (const auto* frequency = Find(*baseline, "frequencyHz"); frequency != nullptr) {
            metadata.frequency_hz = NumberValue(frequency);
            if (!metadata.frequency_hz.has_value() || *metadata.frequency_hz <= 0.0) {
                return Failure(CircuitLoadError::InvalidValue);
            }
        }
        if (const auto* tolerance = Find(*baseline, "tolerance"); tolerance != nullptr) {
            metadata.tolerance = NumberValue(tolerance);
            if (!metadata.tolerance.has_value() || *metadata.tolerance < 0.0) {
                return Failure(CircuitLoadError::InvalidValue);
            }
        }
        circuit.measurement_baseline = std::move(metadata);
    }
    return {.circuit = std::move(circuit), .errors = {}};
}

} // namespace solo_rig
