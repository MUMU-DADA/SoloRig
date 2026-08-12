#include "content/theme.hpp"

#include "content/json.hpp"

#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <variant>

namespace solo_rig {

namespace {

const JsonValue* Find(const JsonValue::Object& object, std::string_view key) {
    const auto iterator = object.find(key);
    return iterator == object.end() ? nullptr : &iterator->second;
}

const ThemeDefinition kDarkTheme{
    .id = "solorig.dark",
    .display_name = "SoloRig Dark",
    .colors = {{"background", "#17191D"}, {"surface", "#262A30"}, {"text", "#F1F5F9"},
               {"accent", "#4FD1C5"}, {"warning", "#FB7185"}},
    .metrics = {{"gridSize", 16.0}, {"cornerRadius", 8.0}},
};

const ThemeDefinition kLightTheme{
    .id = "solorig.light",
    .display_name = "SoloRig Light",
    .colors = {{"background", "#F4F7FB"}, {"surface", "#FFFFFF"}, {"text", "#172033"},
               {"accent", "#0F766E"}, {"warning", "#BE123C"}},
    .metrics = {{"gridSize", 16.0}, {"cornerRadius", 8.0}},
};

} // namespace

bool IsHexColor(std::string_view value) noexcept {
    if ((value.size() != 7U && value.size() != 9U) || value.front() != '#') {
        return false;
    }
    for (const auto character : value.substr(1)) {
        if (!std::isxdigit(static_cast<unsigned char>(character))) {
            return false;
        }
    }
    return true;
}

std::variant<ThemeDefinition, ThemeLoadError> ParseTheme(std::string_view source) {
    const auto parsed = ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        return ThemeLoadError::InvalidJson;
    }
    const auto& root = *parsed.value->AsObject();
    const auto* schema = Find(root, "schemaVersion");
    const auto* id = Find(root, "id");
    const auto* display_name = Find(root, "displayName");
    const auto* colors = Find(root, "colors");
    const auto* metrics = Find(root, "metrics");
    if (schema == nullptr || id == nullptr || display_name == nullptr || colors == nullptr || metrics == nullptr) {
        return ThemeLoadError::MissingField;
    }
    if (schema->AsNumber() == nullptr || *schema->AsNumber() != 1.0) {
        return ThemeLoadError::InvalidSchema;
    }
    if (id->AsString() == nullptr || display_name->AsString() == nullptr || colors->AsObject() == nullptr ||
        metrics->AsObject() == nullptr) {
        return ThemeLoadError::MissingField;
    }

    ThemeDefinition theme{.id = *id->AsString(), .display_name = *display_name->AsString()};
    if (theme.id.empty() || theme.display_name.empty()) {
        return ThemeLoadError::MissingField;
    }
    for (const auto& [name, color] : *colors->AsObject()) {
        if (color.AsString() == nullptr || !IsHexColor(*color.AsString())) {
            return ThemeLoadError::InvalidColor;
        }
        theme.colors.emplace(name, *color.AsString());
    }
    for (const auto& [name, metric] : *metrics->AsObject()) {
        if (metric.AsNumber() == nullptr || !std::isfinite(*metric.AsNumber()) || *metric.AsNumber() < 0.0) {
            return ThemeLoadError::InvalidMetric;
        }
        theme.metrics.emplace(name, *metric.AsNumber());
    }
    return theme;
}

std::variant<ThemeDefinition, ThemeLoadError> LoadThemeFile(const std::filesystem::path& source) {
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        return ThemeLoadError::CannotRead;
    }
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return ParseTheme(contents);
}

const ThemeDefinition& DefaultDarkTheme() {
    return kDarkTheme;
}

const ThemeDefinition& DefaultLightTheme() {
    return kLightTheme;
}

} // namespace solo_rig
