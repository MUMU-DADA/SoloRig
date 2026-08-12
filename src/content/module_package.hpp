#pragma once

#include "core/effect_descriptor.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace solo_rig {

enum class ModulePackageLoadError {
    MissingManifest,
    CannotReadManifest,
    InvalidJson,
    InvalidSchema,
    MissingRequiredField,
    InvalidField,
    UnsafeAssetPath,
    DescriptorInvalid,
};

struct ModulePackage {
    EffectDescriptor descriptor;
    std::filesystem::path root_directory;
    std::filesystem::path logic_path;
    std::optional<std::filesystem::path> custom_skin_path;
};

struct ModulePackageLoadResult {
    std::optional<ModulePackage> package;
    std::vector<ModulePackageLoadError> errors;
};

[[nodiscard]] bool IsSafeRelativeAssetPath(std::string_view path);
[[nodiscard]] ModulePackageLoadResult LoadModulePackage(const std::filesystem::path& root_directory);

} // namespace solo_rig
