#include "content/circuit_v2.hpp"

#include "content/json.hpp"

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
            circuit.supplies.push_back({.id = *id, .positive_node = *positive, .negative_node = *negative, .voltage = *voltage});
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
        if (pins->size() != 2U && !(is_potentiometer && pins->size() == 3U) &&
            !(is_controlled_source && pins->size() == 4U)) {
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
                const auto saturation = Find(*model_object, "saturationCurrent") == nullptr
                    ? std::optional<double>(1.0e-9) : NumberValue(Find(*model_object, "saturationCurrent"));
                const auto thermal = Find(*model_object, "thermalVoltage") == nullptr
                    ? std::optional<double>(0.02585) : NumberValue(Find(*model_object, "thermalVoltage"));
                const auto max_step = Find(*model_object, "maxStep") == nullptr
                    ? std::optional<double>(1.0) : NumberValue(Find(*model_object, "maxStep"));
                if (*type != CircuitComponentType::Diode || !kind.has_value() || *kind != "shockley" ||
                    !saturation.has_value() || !thermal.has_value() || !max_step.has_value() ||
                    *saturation <= 0.0 || *thermal <= 0.0 || *max_step <= 0.0) {
                    return Failure(CircuitLoadError::InvalidValue);
                }
                parsed_component.model = "inline-shockley";
                parsed_component.diode_model = CircuitDiodeModel{.kind = *kind,
                                                                   .saturation_current = *saturation,
                                                                   .thermal_voltage = *thermal,
                                                                   .max_step = *max_step};
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
        if (*type == CircuitComponentType::Diode && !parsed_component.diode_model.has_value()) {
            return Failure(CircuitLoadError::InvalidValue);
        }

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
            if (!states.has_value() || states->empty() || !initial.has_value() || *initial >= states->size()) {
                return Failure(CircuitLoadError::InvalidValue);
            }
            std::unordered_set<std::string> state_ids(states->begin(), states->end());
            if (state_ids.size() != states->size()) {
                return Failure(CircuitLoadError::DuplicateId);
            }
            parsed_component.switch_spec = CircuitSwitchSpec{.states = *states, .initial_state = *initial};
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
        circuit.components.push_back(std::move(parsed_component));
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
