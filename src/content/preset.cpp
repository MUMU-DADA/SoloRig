#include "content/preset.hpp"

#include "content/json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <system_error>
#include <string_view>

namespace solo_rig {

namespace {

const JsonValue* Find(const JsonValue::Object& object, std::string_view key) {
    const auto iterator = object.find(key);
    return iterator == object.end() ? nullptr : &iterator->second;
}

std::optional<std::string> StringField(const JsonValue::Object& object, std::string_view key) {
    const auto* value = Find(object, key);
    return value != nullptr && value->AsString() != nullptr ? std::optional<std::string>(*value->AsString()) : std::nullopt;
}

std::string PathToUtf8(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    std::string result;
    result.reserve(encoded.size());
    for (const auto character : encoded) {
        result.push_back(static_cast<char>(character));
    }
    return result;
}

std::filesystem::path PathFromUtf8(std::string_view text) {
    std::u8string encoded;
    encoded.reserve(text.size());
    for (const auto character : text) {
        encoded.push_back(static_cast<char8_t>(character));
    }
    return std::filesystem::path(encoded);
}

std::string ExternalModuleSourceKindName(ExternalModuleSourceKind source_kind) {
    switch (source_kind) {
    case ExternalModuleSourceKind::PackageDirectory:
        return "package";
    case ExternalModuleSourceKind::AnalogCircuitFile:
        return "circuit";
    case ExternalModuleSourceKind::NeuralAmpModel:
        return "nam";
    case ExternalModuleSourceKind::ImpulseResponse:
        return "ir";
    }
    return "package";
}

std::optional<ExternalModuleSourceKind> ParseExternalModuleSourceKind(std::string_view text) {
    if (text == "package") return ExternalModuleSourceKind::PackageDirectory;
    if (text == "circuit") return ExternalModuleSourceKind::AnalogCircuitFile;
    if (text == "nam") return ExternalModuleSourceKind::NeuralAmpModel;
    if (text == "ir") return ExternalModuleSourceKind::ImpulseResponse;
    return std::nullopt;
}

std::optional<double> NumberField(const JsonValue::Object& object, std::string_view key) {
    const auto* value = Find(object, key);
    return value != nullptr && value->AsNumber() != nullptr ? std::optional<double>(*value->AsNumber()) : std::nullopt;
}

std::optional<ThemeDefinition> ThemeField(const JsonValue::Object& object) {
    const auto id = StringField(object, "id");
    const auto display_name = StringField(object, "displayName");
    const auto* colors = Find(object, "colors");
    const auto* metrics = Find(object, "metrics");
    if (!id.has_value() || !display_name.has_value() || colors == nullptr || metrics == nullptr ||
        colors->AsObject() == nullptr || metrics->AsObject() == nullptr || id->empty() || display_name->empty()) {
        return std::nullopt;
    }
    ThemeDefinition theme{.id = *id, .display_name = *display_name};
    for (const auto& [name, value] : *colors->AsObject()) {
        if (value.AsString() == nullptr || !IsHexColor(*value.AsString())) {
            return std::nullopt;
        }
        theme.colors.emplace(name, *value.AsString());
    }
    for (const auto& [name, value] : *metrics->AsObject()) {
        if (value.AsNumber() == nullptr || !std::isfinite(*value.AsNumber()) || *value.AsNumber() < 0.0) {
            return std::nullopt;
        }
        theme.metrics.emplace(name, *value.AsNumber());
    }
    return theme;
}

std::optional<std::uint8_t> MidiByteField(const JsonValue::Object& object, std::string_view key,
                                          std::uint8_t maximum) {
    const auto value = NumberField(object, key);
    if (!value.has_value() || !std::isfinite(*value) || std::floor(*value) != *value || *value < 0.0 ||
        *value > static_cast<double>(maximum)) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(*value);
}

std::optional<MidiBinding> MidiBindingField(const JsonValue::Object& object) {
    const auto message_type = StringField(object, "messageType");
    const auto channel = MidiByteField(object, "channel", 15U);
    const auto number = MidiByteField(object, "number", 127U);
    const auto target_kind = StringField(object, "targetKind");
    const auto target_id = StringField(object, "targetId");
    const auto minimum = NumberField(object, "minimum");
    const auto maximum = NumberField(object, "maximum");
    if (!message_type.has_value() || !channel.has_value() || !number.has_value() || !target_kind.has_value() ||
        !target_id.has_value() || !minimum.has_value() || !maximum.has_value() ||
        !std::isfinite(*minimum) || !std::isfinite(*maximum)) {
        return std::nullopt;
    }
    const auto parsed_message_type = *message_type == "controlChange"
                                         ? MidiMessageType::ControlChange
                                         : *message_type == "programChange" ? MidiMessageType::ProgramChange
                                                                              : MidiMessageType{};
    const auto parsed_target_kind = *target_kind == "parameter"
                                        ? MidiTargetKind::Parameter
                                        : *target_kind == "preset" ? MidiTargetKind::Preset : MidiTargetKind{};
    if ((*message_type != "controlChange" && *message_type != "programChange") ||
        (*target_kind != "parameter" && *target_kind != "preset")) {
        return std::nullopt;
    }
    MidiBinding binding{.message_type = parsed_message_type,
                        .channel = *channel,
                        .number = *number,
                        .target_kind = parsed_target_kind,
                        .target_id = *target_id,
                        .minimum = static_cast<float>(*minimum),
                        .maximum = static_cast<float>(*maximum)};
    return IsValidMidiBinding(binding) ? std::optional<MidiBinding>(std::move(binding)) : std::nullopt;
}

} // namespace

std::vector<PresetValidationError> ValidatePreset(const RigPreset& preset) {
    std::vector<PresetValidationError> errors;
    if (preset.name.empty()) {
        errors.push_back(PresetValidationError::MissingName);
    }
    std::set<std::string, std::less<>> instance_ids;
    for (const auto& node : preset.nodes) {
        if (node.instance_id.empty()) {
            errors.push_back(PresetValidationError::MissingInstanceId);
        } else if (!instance_ids.insert(node.instance_id).second) {
            errors.push_back(PresetValidationError::DuplicateInstanceId);
        }
        if (node.module_id.empty()) {
            errors.push_back(PresetValidationError::MissingModuleId);
        }
        if (!std::isfinite(node.layout_x) || !std::isfinite(node.layout_y)) {
            errors.push_back(PresetValidationError::NonFiniteLayoutPosition);
        }
        for (const auto& [parameter_id, value] : node.parameter_values) {
            static_cast<void>(parameter_id);
            if (!std::isfinite(value)) {
                errors.push_back(PresetValidationError::NonFiniteParameterValue);
            }
        }
    }
    if (preset.midi_bindings.has_value()) {
        for (const auto& binding : *preset.midi_bindings) {
            bool target_valid = true;
            if (binding.target_kind == MidiTargetKind::Parameter) {
                const auto separator = binding.target_id.rfind('.');
                target_valid = separator != std::string::npos && separator > 0U &&
                               separator + 1U < binding.target_id.size();
                if (target_valid) {
                    const auto instance_id = std::string_view(binding.target_id).substr(0U, separator);
                    const auto parameter_id = std::string_view(binding.target_id).substr(separator + 1U);
                    const auto node = std::ranges::find_if(preset.nodes, [instance_id](const PresetNode& candidate) {
                        return candidate.instance_id == instance_id;
                    });
                    target_valid = node != preset.nodes.end() && node->parameter_values.contains(parameter_id);
                }
            }
            if (!IsValidMidiBinding(binding) || !target_valid) {
                errors.push_back(PresetValidationError::InvalidMidiBinding);
            }
        }
    }
    for (const auto& connection : preset.connections) {
        if (connection.source_instance_id.empty() || connection.destination_instance_id.empty()) {
            errors.push_back(PresetValidationError::MissingConnectionEndpoint);
        } else if (!instance_ids.contains(connection.source_instance_id) ||
                   !instance_ids.contains(connection.destination_instance_id)) {
            errors.push_back(PresetValidationError::UnknownConnectionEndpoint);
        }
    }
    std::set<std::string, std::less<>> package_ids;
    for (const auto& package : preset.external_module_packages) {
        const auto direct_asset = package.source_kind != ExternalModuleSourceKind::PackageDirectory;
        if (package.module_id.empty() || package.root_directory.empty() ||
            (direct_asset && package.asset_path.empty()) || (!direct_asset && !package.asset_path.empty()) ||
            !package_ids.insert(package.module_id).second) {
            errors.push_back(PresetValidationError::InvalidExternalModulePackage);
        }
    }
    return errors;
}

std::string SerializePreset(const RigPreset& preset) {
    const bool has_stable_id = !preset.preset_id.empty();
    std::string serialized = "{\"schemaVersion\":" + std::to_string(has_stable_id ? kCurrentPresetSchemaVersion : 1) +
                             (has_stable_id ? ",\"presetId\":\"" + EscapeJsonString(preset.preset_id) + "\"" : "") +
                             ",\"name\":\"" + EscapeJsonString(preset.name) + "\",\"nodes\":[";
    for (std::size_t index = 0; index < preset.nodes.size(); ++index) {
        const auto& node = preset.nodes[index];
        if (index != 0) {
            serialized += ',';
        }
        serialized += "{\"instanceId\":\"" + EscapeJsonString(node.instance_id) + "\",\"moduleId\":\"" +
                      EscapeJsonString(node.module_id) + "\",\"x\":" + std::to_string(node.layout_x) + ",\"y\":" +
                      std::to_string(node.layout_y);
        if (node.bypassed) {
            serialized += ",\"bypassed\":true";
        }
        serialized += ",\"parameters\":{";
        std::size_t parameter_index = 0;
        for (const auto& [id, value] : node.parameter_values) {
            if (parameter_index++ != 0) {
                serialized += ',';
            }
            serialized += "\"" + EscapeJsonString(id) + "\":" + std::to_string(value);
        }
        serialized += "}}";
    }
    serialized += "],\"connections\":[";
    for (std::size_t index = 0; index < preset.connections.size(); ++index) {
        const auto& connection = preset.connections[index];
        if (index != 0) {
            serialized += ',';
        }
        serialized += "{\"source\":\"" + EscapeJsonString(connection.source_instance_id) + "\",\"destination\":\"" +
                      EscapeJsonString(connection.destination_instance_id) + "\"}";
    }
    serialized += "]";
    if (preset.theme.has_value()) {
        serialized += ",\"theme\":{\"id\":\"" + EscapeJsonString(preset.theme->id) +
                      "\",\"displayName\":\"" + EscapeJsonString(preset.theme->display_name) +
                      "\",\"colors\":{";
        std::size_t color_index = 0;
        for (const auto& [name, value] : preset.theme->colors) {
            if (color_index++ != 0U) {
                serialized += ',';
            }
            serialized += "\"" + EscapeJsonString(name) + "\":\"" + EscapeJsonString(value) + "\"";
        }
        serialized += "},\"metrics\":{";
        std::size_t metric_index = 0;
        for (const auto& [name, value] : preset.theme->metrics) {
            if (metric_index++ != 0U) {
                serialized += ',';
            }
            serialized += "\"" + EscapeJsonString(name) + "\":" + std::to_string(value);
        }
        serialized += "}}";
    }
    if (preset.midi_bindings.has_value()) {
        serialized += ",\"midiBindings\":[";
        for (std::size_t index = 0; index < preset.midi_bindings->size(); ++index) {
            const auto& binding = (*preset.midi_bindings)[index];
            if (index != 0U) {
                serialized += ',';
            }
            serialized += "{\"messageType\":\"";
            serialized += binding.message_type == MidiMessageType::ControlChange ? "controlChange" : "programChange";
            serialized += "\",\"channel\":" + std::to_string(binding.channel) +
                          ",\"number\":" + std::to_string(binding.number) + ",\"targetKind\":\"";
            serialized += binding.target_kind == MidiTargetKind::Parameter ? "parameter" : "preset";
            serialized += "\",\"targetId\":\"" + EscapeJsonString(binding.target_id) +
                          "\",\"minimum\":" + std::to_string(binding.minimum) +
                          ",\"maximum\":" + std::to_string(binding.maximum) + "}";
        }
        serialized += "]";
    }
    if (!preset.external_module_packages.empty()) {
        serialized += ",\"modulePackages\":[";
        for (std::size_t index = 0; index < preset.external_module_packages.size(); ++index) {
            const auto& package = preset.external_module_packages[index];
            if (index != 0U) {
                serialized += ',';
            }
            serialized += "{\"moduleId\":\"" + EscapeJsonString(package.module_id) +
                          "\",\"rootDirectory\":\"" + EscapeJsonString(PathToUtf8(package.root_directory)) + "\"";
            if (package.source_kind != ExternalModuleSourceKind::PackageDirectory) {
                serialized += ",\"sourceKind\":\"" + ExternalModuleSourceKindName(package.source_kind) +
                    "\",\"assetPath\":\"" + EscapeJsonString(PathToUtf8(package.asset_path)) + "\"";
            }
            serialized += "}";
        }
        serialized += "]";
    }
    return serialized + "}";
}

std::optional<RigPreset> DeserializePreset(std::string_view source) {
    const auto parsed = ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        return std::nullopt;
    }
    const auto& root = *parsed.value->AsObject();
    const auto schema = NumberField(root, "schemaVersion");
    const auto name = StringField(root, "name");
    const auto* nodes = Find(root, "nodes");
    const auto* connections = Find(root, "connections");
    if (!schema.has_value() || (*schema != 1.0 && *schema != static_cast<double>(kCurrentPresetSchemaVersion)) ||
        !name.has_value() || nodes == nullptr || connections == nullptr ||
        nodes->AsArray() == nullptr || connections->AsArray() == nullptr) {
        return std::nullopt;
    }

    RigPreset preset{.name = *name};
    if (*schema == static_cast<double>(kCurrentPresetSchemaVersion)) {
        const auto preset_id = StringField(root, "presetId");
        if (!preset_id.has_value() || preset_id->empty()) {
            return std::nullopt;
        }
        preset.preset_id = *preset_id;
    }
    for (const auto& node_value : *nodes->AsArray()) {
        const auto* node = node_value.AsObject();
        if (node == nullptr) {
            return std::nullopt;
        }
        const auto instance_id = StringField(*node, "instanceId");
        const auto module_id = StringField(*node, "moduleId");
        const auto x = NumberField(*node, "x");
        const auto y = NumberField(*node, "y");
        const auto* bypassed = Find(*node, "bypassed");
        const auto* parameters = Find(*node, "parameters");
        if (!instance_id.has_value() || !module_id.has_value() || !x.has_value() || !y.has_value() || parameters == nullptr ||
            parameters->AsObject() == nullptr) {
            return std::nullopt;
        }
        bool node_bypassed = false;
        if (bypassed != nullptr) {
            if (bypassed->AsBoolean() == nullptr) {
                return std::nullopt;
            }
            node_bypassed = *bypassed->AsBoolean();
        }
        PresetNode preset_node{.instance_id = *instance_id,
                               .module_id = *module_id,
                               .layout_x = *x,
                               .layout_y = *y,
                               .bypassed = node_bypassed};
        for (const auto& [parameter_id, parameter_value] : *parameters->AsObject()) {
            if (parameter_value.AsNumber() == nullptr) {
                return std::nullopt;
            }
            preset_node.parameter_values.emplace(parameter_id, static_cast<float>(*parameter_value.AsNumber()));
        }
        preset.nodes.push_back(std::move(preset_node));
    }
    for (const auto& connection_value : *connections->AsArray()) {
        const auto* connection = connection_value.AsObject();
        if (connection == nullptr) {
            return std::nullopt;
        }
        const auto source_id = StringField(*connection, "source");
        const auto destination_id = StringField(*connection, "destination");
        if (!source_id.has_value() || !destination_id.has_value()) {
            return std::nullopt;
        }
        preset.connections.push_back({.source_instance_id = *source_id, .destination_instance_id = *destination_id});
    }
    if (const auto* theme = Find(root, "theme"); theme != nullptr) {
        if (theme->AsObject() == nullptr) {
            return std::nullopt;
        }
        const auto parsed_theme = ThemeField(*theme->AsObject());
        if (!parsed_theme.has_value()) {
            return std::nullopt;
        }
        preset.theme = *parsed_theme;
    }
    if (const auto* bindings = Find(root, "midiBindings"); bindings != nullptr) {
        if (bindings->AsArray() == nullptr) {
            return std::nullopt;
        }
        std::vector<MidiBinding> parsed_bindings;
        parsed_bindings.reserve(bindings->AsArray()->size());
        for (const auto& binding_value : *bindings->AsArray()) {
            const auto* binding = binding_value.AsObject();
            if (binding == nullptr) {
                return std::nullopt;
            }
            const auto parsed_binding = MidiBindingField(*binding);
            if (!parsed_binding.has_value()) {
                return std::nullopt;
            }
            parsed_bindings.push_back(*parsed_binding);
        }
        preset.midi_bindings = std::move(parsed_bindings);
    }
    if (const auto* packages = Find(root, "modulePackages"); packages != nullptr) {
        if (packages->AsArray() == nullptr) {
            return std::nullopt;
        }
        for (const auto& package_value : *packages->AsArray()) {
            const auto* package = package_value.AsObject();
            if (package == nullptr) {
                return std::nullopt;
            }
            const auto module_id = StringField(*package, "moduleId");
            const auto root_directory = StringField(*package, "rootDirectory");
            if (!module_id.has_value() || module_id->empty() || !root_directory.has_value() || root_directory->empty()) {
                return std::nullopt;
            }
            const auto source_kind = ParseExternalModuleSourceKind(
                StringField(*package, "sourceKind").value_or("package"));
            const auto asset_path = StringField(*package, "assetPath");
            if (!source_kind.has_value() ||
                (*source_kind == ExternalModuleSourceKind::PackageDirectory
                     ? asset_path.has_value()
                     : !asset_path.has_value() || asset_path->empty())) {
                return std::nullopt;
            }
            preset.external_module_packages.push_back({.module_id = *module_id,
                                                       .root_directory = PathFromUtf8(*root_directory),
                                                       .source_kind = *source_kind,
                                                       .asset_path = asset_path.has_value()
                                                           ? PathFromUtf8(*asset_path) : std::filesystem::path{}});
        }
    }
    return ValidatePreset(preset).empty() ? std::optional<RigPreset>(std::move(preset)) : std::nullopt;
}

std::optional<PresetFileError> SavePreset(const std::filesystem::path& destination, const RigPreset& preset) {
    if (!ValidatePreset(preset).empty()) {
        return PresetFileError::InvalidPreset;
    }
    const auto temporary_path = destination.string() + ".tmp";
    {
        std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            return PresetFileError::CannotWrite;
        }
        output << SerializePreset(preset);
        if (!output) {
            return PresetFileError::CannotWrite;
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary_path, destination, error);
    if (error) {
        std::filesystem::remove(destination, error);
        error.clear();
        std::filesystem::rename(temporary_path, destination, error);
    }
    return error ? std::optional<PresetFileError>(PresetFileError::CannotWrite) : std::nullopt;
}

std::variant<RigPreset, PresetFileError> LoadPreset(const std::filesystem::path& source) {
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        return PresetFileError::CannotRead;
    }
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto json = ParseJson(contents);
    if (!json.value.has_value()) {
        return PresetFileError::InvalidJson;
    }
    const auto parsed = DeserializePreset(contents);
    return parsed.has_value() ? std::variant<RigPreset, PresetFileError>(std::move(*parsed)) :
                                std::variant<RigPreset, PresetFileError>(PresetFileError::InvalidSchema);
}

} // namespace solo_rig
