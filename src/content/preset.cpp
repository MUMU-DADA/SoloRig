#include "content/preset.hpp"

#include "content/json.hpp"

#include <cmath>
#include <fstream>
#include <set>
#include <system_error>

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

std::optional<double> NumberField(const JsonValue::Object& object, std::string_view key) {
    const auto* value = Find(object, key);
    return value != nullptr && value->AsNumber() != nullptr ? std::optional<double>(*value->AsNumber()) : std::nullopt;
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
    for (const auto& connection : preset.connections) {
        if (connection.source_instance_id.empty() || connection.destination_instance_id.empty()) {
            errors.push_back(PresetValidationError::MissingConnectionEndpoint);
        } else if (!instance_ids.contains(connection.source_instance_id) ||
                   !instance_ids.contains(connection.destination_instance_id)) {
            errors.push_back(PresetValidationError::UnknownConnectionEndpoint);
        }
    }
    return errors;
}

std::string SerializePreset(const RigPreset& preset) {
    std::string serialized = "{\"schemaVersion\":1,\"name\":\"" + EscapeJsonString(preset.name) + "\",\"nodes\":[";
    for (std::size_t index = 0; index < preset.nodes.size(); ++index) {
        const auto& node = preset.nodes[index];
        if (index != 0) {
            serialized += ',';
        }
        serialized += "{\"instanceId\":\"" + EscapeJsonString(node.instance_id) + "\",\"moduleId\":\"" +
                      EscapeJsonString(node.module_id) + "\",\"x\":" + std::to_string(node.layout_x) + ",\"y\":" +
                      std::to_string(node.layout_y) + ",\"parameters\":{";
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
    return serialized + "]}";
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
    if (!schema.has_value() || *schema != 1.0 || !name.has_value() || nodes == nullptr || connections == nullptr ||
        nodes->AsArray() == nullptr || connections->AsArray() == nullptr) {
        return std::nullopt;
    }

    RigPreset preset{.name = *name};
    for (const auto& node_value : *nodes->AsArray()) {
        const auto* node = node_value.AsObject();
        if (node == nullptr) {
            return std::nullopt;
        }
        const auto instance_id = StringField(*node, "instanceId");
        const auto module_id = StringField(*node, "moduleId");
        const auto x = NumberField(*node, "x");
        const auto y = NumberField(*node, "y");
        const auto* parameters = Find(*node, "parameters");
        if (!instance_id.has_value() || !module_id.has_value() || !x.has_value() || !y.has_value() || parameters == nullptr ||
            parameters->AsObject() == nullptr) {
            return std::nullopt;
        }
        PresetNode preset_node{.instance_id = *instance_id, .module_id = *module_id, .layout_x = *x, .layout_y = *y};
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
    const auto parsed = DeserializePreset(contents);
    return parsed.has_value() ? std::variant<RigPreset, PresetFileError>(std::move(*parsed)) :
                                std::variant<RigPreset, PresetFileError>(PresetFileError::InvalidJson);
}

} // namespace solo_rig
