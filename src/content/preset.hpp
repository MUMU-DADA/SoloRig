#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace solo_rig {

struct PresetNode {
    std::string instance_id;
    std::string module_id;
    double layout_x{};
    double layout_y{};
    std::map<std::string, float, std::less<>> parameter_values;
};

struct PresetConnection {
    std::string source_instance_id;
    std::string destination_instance_id;
};

struct RigPreset {
    std::string name;
    std::vector<PresetNode> nodes;
    std::vector<PresetConnection> connections;
};

enum class PresetValidationError {
    MissingName,
    MissingInstanceId,
    DuplicateInstanceId,
    MissingModuleId,
    NonFiniteLayoutPosition,
    NonFiniteParameterValue,
    MissingConnectionEndpoint,
    UnknownConnectionEndpoint,
};

enum class PresetFileError {
    InvalidPreset,
    CannotWrite,
    CannotRead,
    InvalidJson,
    InvalidSchema,
};

[[nodiscard]] std::vector<PresetValidationError> ValidatePreset(const RigPreset& preset);
[[nodiscard]] std::string SerializePreset(const RigPreset& preset);
[[nodiscard]] std::optional<RigPreset> DeserializePreset(std::string_view source);
[[nodiscard]] std::optional<PresetFileError> SavePreset(const std::filesystem::path& destination,
                                                         const RigPreset& preset);
[[nodiscard]] std::variant<RigPreset, PresetFileError> LoadPreset(const std::filesystem::path& source);

} // namespace solo_rig
