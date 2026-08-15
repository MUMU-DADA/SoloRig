#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace solo_rig {

struct ThemeDefinition {
    std::string id;
    std::string display_name;
    std::map<std::string, std::string, std::less<>> colors;
    std::map<std::string, double, std::less<>> metrics;
};

enum class ThemeLoadError {
    CannotRead,
    ResourceLimit,
    InvalidJson,
    InvalidSchema,
    MissingField,
    InvalidColor,
    InvalidMetric,
};

[[nodiscard]] bool IsHexColor(std::string_view value) noexcept;
[[nodiscard]] std::variant<ThemeDefinition, ThemeLoadError> ParseTheme(std::string_view source);
[[nodiscard]] std::variant<ThemeDefinition, ThemeLoadError> LoadThemeFile(const std::filesystem::path& source);
[[nodiscard]] const ThemeDefinition& DefaultDarkTheme();
[[nodiscard]] const ThemeDefinition& DefaultLightTheme();

} // namespace solo_rig
