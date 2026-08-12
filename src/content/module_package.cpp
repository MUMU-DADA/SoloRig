#include "content/module_package.hpp"

#include "content/json.hpp"

#include <cmath>
#include <fstream>
#include <iterator>

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
    return {.package = std::move(package), .errors = {}};
}

} // namespace solo_rig
