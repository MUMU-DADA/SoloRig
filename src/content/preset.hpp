#pragma once

#include "content/theme.hpp"
#include "control/midi_mapping.hpp"

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
    // Empty identifies a legacy schema-v1 document. New schema-v2 documents
    // persist this stable identity; preparation fills it for legacy content.
    std::string preset_id;
    std::string name;
    std::vector<PresetNode> nodes;
    std::vector<PresetConnection> connections;
    // Optional schema-v2 metadata extension. Older files omit it and retain
    // the built-in dark theme fallback.
    std::optional<ThemeDefinition> theme;
    // Optional schema-v2 control-plane extension. Device identities are
    // intentionally not persisted; bindings target logical preset/module IDs.
    std::optional<std::vector<MidiBinding>> midi_bindings;
};

inline constexpr int kCurrentPresetSchemaVersion = 2;

enum class PresetValidationError {
    MissingName,
    MissingInstanceId,
    DuplicateInstanceId,
    MissingModuleId,
    NonFiniteLayoutPosition,
    NonFiniteParameterValue,
    MissingConnectionEndpoint,
    UnknownConnectionEndpoint,
    InvalidMidiBinding,
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
