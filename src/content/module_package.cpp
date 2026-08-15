#include "content/module_package.hpp"

#include "content/json.hpp"
#include "dsp/impulse_response.hpp"
#include "dsp/nam_processor.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <unordered_set>

namespace solo_rig {

namespace {

const JsonValue* Find(const JsonValue::Object& object, std::string_view key) {
    const auto iterator = object.find(key);
    return iterator == object.end() ? nullptr : &iterator->second;
}

std::optional<std::string> ReadString(const JsonValue::Object& object, std::string_view key) {
    const auto* value = Find(object, key);
    if (value == nullptr || value->AsString() == nullptr) {
        return std::nullopt;
    }
    return *value->AsString();
}

std::optional<std::uint32_t> ReadUnsigned(const JsonValue::Object& object, std::string_view key) {
    const auto* value = Find(object, key);
    if (value == nullptr || value->AsNumber() == nullptr || *value->AsNumber() < 0.0 ||
        *value->AsNumber() != std::floor(*value->AsNumber())) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value->AsNumber());
}

std::optional<double> ReadFiniteNumber(const JsonValue::Object& object, std::string_view key) {
    const auto* value = Find(object, key);
    if (value == nullptr || value->AsNumber() == nullptr || !std::isfinite(*value->AsNumber())) {
        return std::nullopt;
    }
    return *value->AsNumber();
}

std::optional<std::vector<std::string>> ReadStringArray(const JsonValue::Object& object, std::string_view key) {
    const auto* value = Find(object, key);
    if (value == nullptr || value->AsArray() == nullptr) {
        return std::nullopt;
    }

    std::vector<std::string> result;
    result.reserve(value->AsArray()->size());
    for (const auto& item : *value->AsArray()) {
        const auto* text = item.AsString();
        if (text == nullptr || text->empty()) {
            return std::nullopt;
        }
        result.push_back(*text);
    }
    return result;
}

std::optional<EffectCategory> ParseCategory(const std::string& text) {
    if (text == "analog-circuit") {
        return EffectCategory::AnalogCircuit;
    }
    if (text == "digital-algorithm") {
        return EffectCategory::DigitalAlgorithm;
    }
    if (text == "neural-amp-model") {
        return EffectCategory::NeuralAmpModel;
    }
    if (text == "impulse-response") {
        return EffectCategory::ImpulseResponse;
    }
    if (text == "utility") {
        return EffectCategory::Utility;
    }
    return std::nullopt;
}

std::optional<PortDirection> ParseDirection(const std::string& text) {
    if (text == "input") {
        return PortDirection::Input;
    }
    if (text == "output") {
        return PortDirection::Output;
    }
    return std::nullopt;
}

std::optional<AnalogComponentType> ParseAnalogComponentType(const std::string& text) {
    if (text == "resistor") {
        return AnalogComponentType::Resistor;
    }
    if (text == "capacitor") {
        return AnalogComponentType::Capacitor;
    }
    if (text == "inductor") {
        return AnalogComponentType::Inductor;
    }
    if (text == "diode") {
        return AnalogComponentType::Diode;
    }
    if (text == "potentiometer") {
        return AnalogComponentType::Potentiometer;
    }
    if (text == "op-amp") {
        return AnalogComponentType::OperationalAmplifier;
    }
    if (text == "transistor") {
        return AnalogComponentType::Transistor;
    }
    if (text == "voltage-source") {
        return AnalogComponentType::VoltageSource;
    }
    return std::nullopt;
}

bool RequiresPositiveValue(AnalogComponentType type) {
    return type == AnalogComponentType::Resistor || type == AnalogComponentType::Capacitor ||
        type == AnalogComponentType::Inductor || type == AnalogComponentType::Potentiometer;
}

std::optional<DigitalOperationType> ParseDigitalOperationType(const std::string& text) {
    if (text == "gain") {
        return DigitalOperationType::Gain;
    }
    if (text == "delay") {
        return DigitalOperationType::Delay;
    }
    if (text == "feedback") {
        return DigitalOperationType::Feedback;
    }
    if (text == "mix") {
        return DigitalOperationType::Mix;
    }
    if (text == "filter") {
        return DigitalOperationType::Filter;
    }
    if (text == "waveshaper") {
        return DigitalOperationType::Waveshaper;
    }
    if (text == "dynamics") {
        return DigitalOperationType::Dynamics;
    }
    if (text == "pitch-shift") {
        return DigitalOperationType::PitchShift;
    }
    if (text == "bypass") {
        return DigitalOperationType::Bypass;
    }
    return std::nullopt;
}

bool HasParameter(const EffectDescriptor& descriptor, const std::string& id) {
    for (const auto& parameter : descriptor.parameters) {
        if (parameter.id == id) {
            return true;
        }
    }
    return false;
}

std::optional<ModulePackageLoadError> ValidateCircuitParameterBindings(
    const CircuitNetlist& circuit, const EffectDescriptor& descriptor) {
    for (const auto& binding : circuit.parameter_bindings) {
        const auto parameter = std::find_if(descriptor.parameters.begin(), descriptor.parameters.end(),
            [&binding](const EffectParameter& item) { return item.id == binding.parameter_id; });
        if (parameter == descriptor.parameters.end()) {
            return ModulePackageLoadError::LogicParameterUnknown;
        }
        double minimum{};
        double maximum{};
        if (binding.property == CircuitParameterBindingProperty::PotentiometerPosition) {
            const auto component = std::find_if(circuit.components.begin(), circuit.components.end(),
                [&binding](const CircuitComponent& item) { return item.id == binding.target_id; });
            if (component == circuit.components.end() || !component->potentiometer.has_value()) {
                return ModulePackageLoadError::InvalidLogicDefinition;
            }
            minimum = component->potentiometer->minimum;
            maximum = component->potentiometer->maximum;
        } else if (binding.property == CircuitParameterBindingProperty::SwitchState) {
            minimum = 0.0;
            maximum = 1.0;
        } else {
            minimum = -1.0e6;
            maximum = 1.0e6;
        }
        if (!std::isfinite(parameter->minimum) || !std::isfinite(parameter->maximum) ||
            !std::isfinite(parameter->default_value) || parameter->minimum < minimum ||
            parameter->maximum > maximum) {
            return ModulePackageLoadError::InvalidLogicDefinition;
        }
    }
    return std::nullopt;
}

std::optional<AnalogCircuitLogic> ParseAnalogCircuitLogic(const JsonValue::Object& object) {
    const auto solver = ReadString(object, "solver");
    const auto* component_values = Find(object, "components");
    if (!solver.has_value() || *solver != "modified-nodal-analysis" || component_values == nullptr ||
        component_values->AsArray() == nullptr || component_values->AsArray()->empty()) {
        return std::nullopt;
    }

    AnalogCircuitLogic circuit;
    std::unordered_set<std::string> component_ids;
    for (const auto& component_value : *component_values->AsArray()) {
        const auto* component = component_value.AsObject();
        if (component == nullptr) {
            return std::nullopt;
        }
        const auto id = ReadString(*component, "id");
        const auto type_text = ReadString(*component, "type");
        const auto nodes = ReadStringArray(*component, "nodes");
        const auto type = type_text.has_value() ? ParseAnalogComponentType(*type_text) : std::nullopt;
        if (!id.has_value() || id->empty() || !type.has_value() || !nodes.has_value() || nodes->size() < 2U ||
            !component_ids.insert(*id).second) {
            return std::nullopt;
        }

        const auto* raw_value = Find(*component, "value");
        std::optional<double> value;
        if (raw_value != nullptr) {
            value = ReadFiniteNumber(*component, "value");
            if (!value.has_value()) {
                return std::nullopt;
            }
        }
        if (RequiresPositiveValue(*type) && (!value.has_value() || *value <= 0.0)) {
            return std::nullopt;
        }

        std::optional<std::string> model;
        if (Find(*component, "model") != nullptr) {
            model = ReadString(*component, "model");
            if (!model.has_value() || model->empty()) {
                return std::nullopt;
            }
        }
        circuit.components.push_back({.id = *id, .type = *type, .nodes = *nodes, .value = value, .model = model});
    }
    return circuit;
}

std::optional<DigitalAlgorithmLogic> ParseDigitalAlgorithmLogic(const JsonValue::Object& object,
                                                                 const EffectDescriptor& descriptor,
                                                                 bool& unknown_parameter) {
    unknown_parameter = false;
    const auto algorithm_id = ReadString(object, "algorithm");
    const auto* operation_values = Find(object, "operations");
    if (!algorithm_id.has_value() || algorithm_id->empty() || operation_values == nullptr ||
        operation_values->AsArray() == nullptr || operation_values->AsArray()->empty()) {
        return std::nullopt;
    }

    DigitalAlgorithmLogic algorithm{.algorithm_id = *algorithm_id};
    for (const auto& operation_value : *operation_values->AsArray()) {
        const auto* operation = operation_value.AsObject();
        if (operation == nullptr) {
            return std::nullopt;
        }
        const auto type_text = ReadString(*operation, "type");
        const auto type = type_text.has_value() ? ParseDigitalOperationType(*type_text) : std::nullopt;
        if (!type.has_value()) {
            return std::nullopt;
        }

        std::optional<std::string> parameter_id;
        if (Find(*operation, "parameter") != nullptr) {
            parameter_id = ReadString(*operation, "parameter");
            if (!parameter_id.has_value() || parameter_id->empty()) {
                return std::nullopt;
            }
            if (!HasParameter(descriptor, *parameter_id)) {
                unknown_parameter = true;
                return std::nullopt;
            }
        }
        algorithm.operations.push_back({.type = *type, .parameter_id = std::move(parameter_id)});
    }
    return algorithm;
}

std::optional<ModulePackageLoadError> ParseModuleLogic(const std::filesystem::path& logic_path,
                                                       const EffectDescriptor& descriptor,
                                                       ModuleLogic& logic) {
    if (descriptor.category == EffectCategory::NeuralAmpModel) {
        if (!std::filesystem::is_regular_file(logic_path)) {
            return ModulePackageLoadError::MissingLogicFile;
        }
        auto extension = logic_path.extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        if (extension != ".nam" && extension != ".nam-a2") {
            return ModulePackageLoadError::InvalidLogicDefinition;
        }
        logic = NeuralAmpModelLogic{.model_path = logic_path};
        return std::nullopt;
    }
    if (descriptor.category == EffectCategory::ImpulseResponse) {
        if (!std::filesystem::is_regular_file(logic_path)) {
            return ModulePackageLoadError::MissingLogicFile;
        }
        auto extension = logic_path.extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        if (extension != ".wav") {
            return ModulePackageLoadError::InvalidLogicDefinition;
        }
        logic = ImpulseResponseLogic{.impulse_response_path = logic_path};
        return std::nullopt;
    }
    if (descriptor.category != EffectCategory::AnalogCircuit && descriptor.category != EffectCategory::DigitalAlgorithm) {
        logic = std::monostate{};
        return std::nullopt;
    }
    if (!std::filesystem::is_regular_file(logic_path)) {
        return ModulePackageLoadError::MissingLogicFile;
    }

    std::ifstream logic_stream(logic_path, std::ios::binary);
    if (!logic_stream) {
        return ModulePackageLoadError::CannotReadLogicFile;
    }
    const std::string logic_source((std::istreambuf_iterator<char>(logic_stream)), std::istreambuf_iterator<char>());
    const auto parsed = ParseJson(logic_source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        return ModulePackageLoadError::InvalidLogicJson;
    }

    const auto& object = *parsed.value->AsObject();
    const auto schema_version = ReadUnsigned(object, "schemaVersion").has_value()
        ? ReadUnsigned(object, "schemaVersion")
        : ReadUnsigned(object, "version");
    const auto kind = ReadString(object, "kind");
    if (!schema_version.has_value()) {
        return ModulePackageLoadError::InvalidLogicDefinition;
    }
    if (*schema_version == 2U && descriptor.category == EffectCategory::AnalogCircuit) {
        const auto circuit = ParseCircuitV2Json(logic_source);
        if (!circuit.circuit.has_value()) {
            return std::any_of(circuit.errors.begin(), circuit.errors.end(), [](CircuitLoadError error) {
                return error == CircuitLoadError::UnsupportedVersion;
            })
                ? ModulePackageLoadError::InvalidLogicSchema
                : ModulePackageLoadError::InvalidLogicDefinition;
        }
        if (const auto error = ValidateCircuitParameterBindings(*circuit.circuit, descriptor); error.has_value()) {
            return error;
        }
        logic = *circuit.circuit;
        return std::nullopt;
    }
    if (!kind.has_value()) {
        return ModulePackageLoadError::InvalidLogicDefinition;
    }
    if (*schema_version != 1U) {
        return ModulePackageLoadError::InvalidLogicSchema;
    }

    if (descriptor.category == EffectCategory::AnalogCircuit) {
        if (*kind != "analog-circuit") {
            return ModulePackageLoadError::LogicCategoryMismatch;
        }
        const auto circuit = ParseAnalogCircuitLogic(object);
        if (!circuit.has_value()) {
            return ModulePackageLoadError::InvalidLogicDefinition;
        }
        logic = *circuit;
        return std::nullopt;
    }

    if (*kind != "digital-algorithm") {
        return ModulePackageLoadError::LogicCategoryMismatch;
    }
    bool unknown_parameter = false;
    const auto algorithm = ParseDigitalAlgorithmLogic(object, descriptor, unknown_parameter);
    if (!algorithm.has_value()) {
        return unknown_parameter ? ModulePackageLoadError::LogicParameterUnknown
                                 : ModulePackageLoadError::InvalidLogicDefinition;
    }
    logic = *algorithm;
    return std::nullopt;
}

} // namespace

bool IsSafeRelativeAssetPath(std::string_view path) {
    const std::filesystem::path candidate{std::string(path)};
    if (path.empty() || candidate.is_absolute() || candidate.has_root_name() || candidate.has_root_directory()) {
        return false;
    }
    for (const auto& component : candidate) {
        if (component == ".." || component == ".") {
            return false;
        }
    }
    return true;
}

ModulePackageLoadResult LoadModulePackage(const std::filesystem::path& root_directory) {
    const auto manifest_path = root_directory / "manifest.json";
    if (!std::filesystem::is_regular_file(manifest_path)) {
        return {.package = std::nullopt, .errors = {ModulePackageLoadError::MissingManifest}};
    }

    std::ifstream manifest_stream(manifest_path, std::ios::binary);
    if (!manifest_stream) {
        return {.package = std::nullopt, .errors = {ModulePackageLoadError::CannotReadManifest}};
    }
    const std::string manifest_source((std::istreambuf_iterator<char>(manifest_stream)), std::istreambuf_iterator<char>());
    const auto parsed = ParseJson(manifest_source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        return {.package = std::nullopt, .errors = {ModulePackageLoadError::InvalidJson}};
    }

    const auto& object = *parsed.value->AsObject();
    const auto schema_version = ReadUnsigned(object, "schemaVersion");
    const auto module_id = ReadString(object, "moduleId");
    const auto display_name = ReadString(object, "displayName");
    const auto category_text = ReadString(object, "category");
    const auto logic_file = ReadString(object, "logicFile");
    if (!schema_version.has_value() || !module_id.has_value() || !display_name.has_value() || !category_text.has_value() ||
        !logic_file.has_value()) {
        return {.package = std::nullopt, .errors = {ModulePackageLoadError::MissingRequiredField}};
    }
    if (*schema_version != 1U) {
        return {.package = std::nullopt, .errors = {ModulePackageLoadError::InvalidSchema}};
    }
    const auto category = ParseCategory(*category_text);
    if (!category.has_value()) {
        return {.package = std::nullopt, .errors = {ModulePackageLoadError::InvalidField}};
    }
    if (!IsSafeRelativeAssetPath(*logic_file)) {
        return {.package = std::nullopt, .errors = {ModulePackageLoadError::UnsafeAssetPath}};
    }

    EffectDescriptor descriptor{
        .module_id = *module_id,
        .display_name = *display_name,
        .api_version = *schema_version,
        .category = *category,
        .logic_entry_point = *logic_file,
        .declared_latency_samples = ReadUnsigned(object, "declaredLatencySamples").value_or(0U),
    };
    const auto* ports = Find(object, "ports");
    if (ports == nullptr || ports->AsArray() == nullptr) {
        return {.package = std::nullopt, .errors = {ModulePackageLoadError::MissingRequiredField}};
    }
    for (const auto& port_value : *ports->AsArray()) {
        const auto* port = port_value.AsObject();
        if (port == nullptr) {
            return {.package = std::nullopt, .errors = {ModulePackageLoadError::InvalidField}};
        }
        const auto port_id = ReadString(*port, "id");
        const auto direction_text = ReadString(*port, "direction");
        const auto channel_count = ReadUnsigned(*port, "maxChannels");
        const auto direction = direction_text.has_value() ? ParseDirection(*direction_text) : std::nullopt;
        if (!port_id.has_value() || !direction.has_value() || !channel_count.has_value()) {
            return {.package = std::nullopt, .errors = {ModulePackageLoadError::InvalidField}};
        }
        descriptor.ports.push_back({.id = *port_id, .direction = *direction, .max_channels = *channel_count});
    }

    if (const auto* parameters = Find(object, "parameters"); parameters != nullptr) {
        if (parameters->AsArray() == nullptr) {
            return {.package = std::nullopt, .errors = {ModulePackageLoadError::InvalidField}};
        }
        for (const auto& parameter_value : *parameters->AsArray()) {
            const auto* parameter = parameter_value.AsObject();
            if (parameter == nullptr) {
                return {.package = std::nullopt, .errors = {ModulePackageLoadError::InvalidField}};
            }
            const auto id = ReadString(*parameter, "id");
            const auto* minimum = Find(*parameter, "minimum");
            const auto* maximum = Find(*parameter, "maximum");
            const auto* default_value = Find(*parameter, "default");
            if (!id.has_value() || minimum == nullptr || maximum == nullptr || default_value == nullptr ||
                minimum->AsNumber() == nullptr || maximum->AsNumber() == nullptr || default_value->AsNumber() == nullptr) {
                return {.package = std::nullopt, .errors = {ModulePackageLoadError::InvalidField}};
            }
            descriptor.parameters.push_back({.id = *id,
                                             .minimum = static_cast<float>(*minimum->AsNumber()),
                                             .maximum = static_cast<float>(*maximum->AsNumber()),
                                             .default_value = static_cast<float>(*default_value->AsNumber())});
        }
    }

    if (!descriptor.Validate().empty()) {
        return {.package = std::nullopt, .errors = {ModulePackageLoadError::DescriptorInvalid}};
    }

    ModulePackage package{
        .descriptor = std::move(descriptor),
        .root_directory = root_directory,
        .logic_path = root_directory / *logic_file,
    };
    if (const auto skin_file = ReadString(object, "skinFile"); skin_file.has_value()) {
        if (!IsSafeRelativeAssetPath(*skin_file)) {
            return {.package = std::nullopt, .errors = {ModulePackageLoadError::UnsafeAssetPath}};
        }
        package.custom_skin_path = root_directory / *skin_file;
    }
    if (const auto logic_error = ParseModuleLogic(package.logic_path, package.descriptor, package.logic);
        logic_error.has_value()) {
        return {.package = std::nullopt, .errors = {*logic_error}};
    }
    return {.package = std::move(package), .errors = {}};
}

ModulePackageLoadResult LoadExternalModuleAsset(const std::filesystem::path& asset_path,
                                                ExternalModuleSourceKind source_kind,
                                                std::string module_id,
                                                std::string display_name) {
    if (source_kind == ExternalModuleSourceKind::PackageDirectory || module_id.empty() ||
        !std::filesystem::is_regular_file(asset_path)) {
        return {.package = std::nullopt, .errors = {ModulePackageLoadError::ExternalAssetInvalid}};
    }
    std::error_code absolute_error;
    const auto absolute_asset = std::filesystem::absolute(asset_path, absolute_error).lexically_normal();
    if (absolute_error || absolute_asset.empty()) {
        return {.package = std::nullopt, .errors = {ModulePackageLoadError::ExternalAssetInvalid}};
    }
    if (display_name.empty()) display_name = absolute_asset.filename().string();
    if (source_kind == ExternalModuleSourceKind::AnalogCircuitFile) {
        std::ifstream circuit_stream(absolute_asset, std::ios::binary);
        if (!circuit_stream) {
            return {.package = std::nullopt, .errors = {ModulePackageLoadError::ExternalAssetInvalid}};
        }
        const std::string circuit_source((std::istreambuf_iterator<char>(circuit_stream)),
                                         std::istreambuf_iterator<char>());
        const auto parsed = ParseCircuitV2Json(circuit_source);
        if (!parsed.circuit.has_value() || !parsed.errors.empty() ||
            !parsed.circuit->parameter_bindings.empty()) {
            // A direct circuit file carries no manifest parameter metadata, so
            // bindings must stay in the full package format rather than being
            // silently exposed as uneditable parameters.
            return {.package = std::nullopt, .errors = {ModulePackageLoadError::ExternalAssetInvalid}};
        }
        EffectDescriptor descriptor{
            .module_id = std::move(module_id),
            .display_name = std::move(display_name),
            .api_version = 1U,
            .category = EffectCategory::AnalogCircuit,
            .logic_entry_point = absolute_asset.filename().string(),
            .declared_latency_samples = 0U,
        };
        for (const auto& port : parsed.circuit->ports) {
            descriptor.ports.push_back({
                .id = port.id,
                .direction = port.direction == CircuitPortDirection::Input
                    ? PortDirection::Input : PortDirection::Output,
                .max_channels = 2U,
            });
        }
        if (!descriptor.Validate().empty()) {
            return {.package = std::nullopt, .errors = {ModulePackageLoadError::DescriptorInvalid}};
        }
        return {.package = ModulePackage{
                     .descriptor = std::move(descriptor),
                     .root_directory = absolute_asset.parent_path(),
                     .logic_path = absolute_asset,
                     .logic = *parsed.circuit,
                 },
                 .errors = {}};
    }
    EffectDescriptor descriptor{
        .module_id = std::move(module_id),
        .display_name = std::move(display_name),
        .api_version = 1U,
        .category = source_kind == ExternalModuleSourceKind::NeuralAmpModel
            ? EffectCategory::NeuralAmpModel : EffectCategory::ImpulseResponse,
        .logic_entry_point = absolute_asset.filename().string(),
        .declared_latency_samples = 0U,
    };
    if (source_kind == ExternalModuleSourceKind::NeuralAmpModel) {
        NamProcessor probe;
        if (probe.LoadModel(absolute_asset).has_value() || !probe.ModelInfo().has_value()) {
            return {.package = std::nullopt, .errors = {ModulePackageLoadError::ExternalAssetInvalid}};
        }
        const auto& info = *probe.ModelInfo();
        descriptor.ports = {
            {.id = "input", .direction = PortDirection::Input, .max_channels = info.input_channels},
            {.id = "output", .direction = PortDirection::Output, .max_channels = info.output_channels},
        };
        if (!descriptor.Validate().empty()) {
            return {.package = std::nullopt, .errors = {ModulePackageLoadError::DescriptorInvalid}};
        }
        return {.package = ModulePackage{
                     .descriptor = std::move(descriptor),
                     .root_directory = absolute_asset.parent_path(),
                     .logic_path = absolute_asset,
                     .logic = NeuralAmpModelLogic{.model_path = absolute_asset},
                 },
                 .errors = {}};
    }
    const auto imported = LoadWaveImpulseResponse(absolute_asset);
    if (!std::holds_alternative<ImportedImpulseResponse>(imported)) {
        return {.package = std::nullopt, .errors = {ModulePackageLoadError::ExternalAssetInvalid}};
    }
    descriptor.ports = {
        {.id = "input", .direction = PortDirection::Input, .max_channels = 1U},
        {.id = "output", .direction = PortDirection::Output, .max_channels = 1U},
    };
    if (!descriptor.Validate().empty()) {
        return {.package = std::nullopt, .errors = {ModulePackageLoadError::DescriptorInvalid}};
    }
    return {.package = ModulePackage{
                 .descriptor = std::move(descriptor),
                 .root_directory = absolute_asset.parent_path(),
                 .logic_path = absolute_asset,
                 .logic = ImpulseResponseLogic{.impulse_response_path = absolute_asset},
             },
             .errors = {}};
}

} // namespace solo_rig
