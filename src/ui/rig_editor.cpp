#include "ui/rig_editor.hpp"

#include "content/json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <ranges>
#include <set>
#include <stack>
#include <system_error>

namespace solo_rig {
namespace {

const PedalSkinDefinition kDefaultSkin{
    .id = "solorig.default-pedal",
    .knob = "standard-knob",
    .toggle = "standard-toggle",
    .button = "standard-button",
    .footswitch = "standard-footswitch",
};

const JsonValue* Find(const JsonValue::Object& object, std::string_view key) {
    const auto iterator = object.find(key);
    return iterator == object.end() ? nullptr : &iterator->second;
}

std::optional<std::string> StringField(const JsonValue::Object& object, std::string_view key) {
    const auto* value = Find(object, key);
    return value != nullptr && value->AsString() != nullptr ? std::optional<std::string>(*value->AsString()) : std::nullopt;
}

} // namespace

const PedalSkinDefinition& DefaultPedalSkin() {
    return kDefaultSkin;
}

RigPreset RigEditorViewModel::DefaultEditorPreset() {
    return RigPreset{
        .preset_id = "solorig.default-editor",
        .name = "SoloRig Default",
        .nodes = {
            {.instance_id = "drive-1",
             .module_id = "org.mumu-dada.solorig.drive",
             .layout_x = 64.0,
             .layout_y = 148.0,
             .parameter_values = {{"drive", 4.0F}, {"outputGain", 1.0F}, {"diodeThreshold", 0.35F}}},
            {.instance_id = "delay-1",
             .module_id = "org.mumu-dada.solorig.digital-delay",
             .layout_x = 316.0,
             .layout_y = 148.0,
             .parameter_values = {{"time", 0.35F}, {"feedback", 0.35F}, {"mix", 0.25F}}},
        },
        .connections = {{.source_instance_id = "drive-1", .destination_instance_id = "delay-1"}},
    };
}

RigEditorViewModel::RigEditorViewModel(RigPreset preset)
    : layout_(DefaultDarkTheme().metrics.at("gridSize")), theme_(DefaultDarkTheme()), skin_(DefaultPedalSkin()) {
    ApplyPreset(std::move(preset), std::nullopt, false);
}

void RigEditorViewModel::SetPreset(RigPreset preset) {
    if (preset.name.empty()) {
        preset = DefaultEditorPreset();
    }
    ApplyPreset(std::move(preset), source_path_, true);
}

void RigEditorViewModel::ApplyPreset(RigPreset preset, std::optional<std::filesystem::path> source, bool dirty) {
    if (preset.name.empty()) {
        preset = DefaultEditorPreset();
    }
    preset_ = std::move(preset);
    if (preset_.theme.has_value()) {
        theme_ = *preset_.theme;
        dark_theme_ = theme_.id.find("dark") != std::string::npos;
    } else {
        // Legacy .srig files have no theme metadata. Do not inherit a theme
        // from the previously open document; use the stable built-in fallback.
        theme_ = DefaultDarkTheme();
        dark_theme_ = true;
    }
    layout_ = PedalLayout(GridSize());
    unresolved_nodes_.clear();
    for (const auto& node : preset_.nodes) {
        const auto position = (std::isfinite(node.layout_x) && std::isfinite(node.layout_y))
            ? PedalPosition{node.layout_x, node.layout_y}
            : PedalPosition{};
        static_cast<void>(layout_.Add(node.instance_id, position));
        if (!IsKnownModule(node.module_id)) {
            unresolved_nodes_.push_back(node.instance_id);
        }
    }
    source_path_ = std::move(source);
    dirty_ = dirty;
    dragging_instance_id_.clear();
    RebuildScene();
}

RigPreset RigEditorViewModel::ExportPreset() const {
    auto result = preset_;
    result.theme = theme_;
    for (auto& node : result.nodes) {
        if (const auto position = layout_.Find(node.instance_id); position.has_value()) {
            node.layout_x = position->x;
            node.layout_y = position->y;
        }
    }
    return result;
}

EditorFileResult RigEditorViewModel::SaveTo(const std::filesystem::path& destination) {
    const auto normalized = NormalizePresetPath(destination);
    if (!normalized.has_value()) {
        SetFileStatus("Save failed: only .srig preset files are accepted.");
        return {.success = false,
                .error = EditorFileError::InvalidExtension,
                .message = file_status_};
    }
    const auto result = solo_rig::SavePreset(*normalized, ExportPreset());
    if (result.has_value()) {
        const auto error = *result;
        const auto mapped = error == PresetFileError::CannotWrite ? EditorFileError::CannotWrite
                                                                    : EditorFileError::InvalidSchema;
        SetFileStatus(error == PresetFileError::CannotWrite ? "Save failed: path is not writable."
                                                              : "Save failed: current preset is invalid.");
        return {.success = false, .error = mapped, .message = file_status_};
    }
    source_path_ = *normalized;
    dirty_ = false;
    RebuildScene();
    SetFileStatus("Saved: " + source_path_->filename().string());
    return {.success = true, .message = file_status_};
}

EditorFileResult RigEditorViewModel::LoadFrom(const std::filesystem::path& source) {
    const auto normalized = NormalizePresetPath(source);
    if (!normalized.has_value()) {
        SetFileStatus("Open failed: only .srig preset files are accepted.");
        return {.success = false,
                .error = EditorFileError::InvalidExtension,
                .message = file_status_};
    }
    const auto loaded = solo_rig::LoadPreset(*normalized);
    if (std::holds_alternative<PresetFileError>(loaded)) {
        const auto error = std::get<PresetFileError>(loaded);
        const auto mapped = MapLoadError(error);
        SetFileStatus("Open failed: " + std::string(
            error == PresetFileError::CannotRead ? "file cannot be read."
                : error == PresetFileError::InvalidJson ? "invalid JSON."
                : "unsupported or invalid preset schema."));
        return {.success = false, .error = mapped, .message = file_status_};
    }
    auto candidate = std::get<RigPreset>(loaded);
    if (HasInvalidTopology(candidate)) {
        SetFileStatus("Open failed: preset contains an invalid cyclic topology.");
        return {.success = false, .error = EditorFileError::InvalidTopology, .message = file_status_};
    }
    ApplyPreset(std::move(candidate), *normalized, false);
    if (!unresolved_nodes_.empty()) {
        SetFileStatus("Loaded with placeholders: missing module assets for " + unresolved_nodes_.front() + ".");
        return {.success = true, .warning = true, .message = file_status_};
    }
    SetFileStatus("Loaded: " + source_path_->filename().string());
    return {.success = true, .message = file_status_};
}

EditorFileResult RigEditorViewModel::SaveCurrent() {
    if (!source_path_.has_value()) {
        SetFileStatus("Save requires a destination path.");
        return {.success = false, .error = EditorFileError::CannotWrite, .message = file_status_};
    }
    return SaveTo(*source_path_);
}

bool RigEditorViewModel::SetNodeParameter(std::string_view instance_id,
                                          std::string_view parameter_id,
                                          float value) {
    const auto node = std::ranges::find_if(preset_.nodes, [instance_id](const PresetNode& candidate) {
        return candidate.instance_id == instance_id;
    });
    if (node == preset_.nodes.end() || !std::isfinite(value)) {
        return false;
    }
    const auto parameter = node->parameter_values.find(parameter_id);
    if (parameter == node->parameter_values.end()) {
        return false;
    }
    parameter->second = value;
    dirty_ = true;
    SetFileStatus("Unsaved changes");
    return true;
}

std::optional<std::vector<MidiBinding>> RigEditorViewModel::MidiBindings() const {
    return preset_.midi_bindings;
}

bool RigEditorViewModel::SetMidiBindings(std::vector<MidiBinding> bindings) {
    auto candidate = preset_;
    candidate.midi_bindings = bindings;
    if (!ValidatePreset(candidate).empty()) {
        return false;
    }
    preset_.midi_bindings = std::move(bindings);
    dirty_ = true;
    SetFileStatus("Unsaved changes");
    RebuildScene();
    return true;
}

bool RigEditorViewModel::IsDirty() const noexcept {
    return dirty_;
}

std::optional<std::filesystem::path> RigEditorViewModel::SourcePath() const {
    return source_path_;
}

std::string_view RigEditorViewModel::FileStatus() const noexcept {
    return file_status_;
}

bool RigEditorViewModel::LoadThemeFile(const std::filesystem::path& source, bool mark_dirty) {
    const auto loaded = solo_rig::LoadThemeFile(source);
    if (std::holds_alternative<ThemeDefinition>(loaded)) {
        SetTheme(std::get<ThemeDefinition>(loaded), mark_dirty);
        return true;
    }
    // Keep the current valid theme when an external resource is missing or
    // malformed; the scene remains renderable with built-in defaults.
    return false;
}

void RigEditorViewModel::SetThemeMode(bool dark) {
    dark_theme_ = dark;
    theme_ = dark ? DefaultDarkTheme() : DefaultLightTheme();
    dirty_ = true;
    SetFileStatus("Unsaved changes");
    RebuildScene();
}

void RigEditorViewModel::SetTheme(ThemeDefinition theme, bool mark_dirty) {
    if (theme.id.empty() || theme.colors.empty()) {
        return;
    }
    dark_theme_ = theme.id.find("dark") != std::string::npos;
    theme_ = std::move(theme);
    if (mark_dirty) {
        dirty_ = true;
        SetFileStatus("Unsaved changes");
    }
    RebuildScene();
}

bool RigEditorViewModel::IsDarkTheme() const noexcept {
    return dark_theme_;
}

bool RigEditorViewModel::LoadSkinFile(const std::filesystem::path& source) {
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        return false;
    }
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto parsed = ParseJson(contents);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        return false;
    }
    const auto& object = *parsed.value->AsObject();
    const auto schema = Find(object, "schemaVersion");
    const auto id = StringField(object, "id");
    const auto* controls = Find(object, "controls");
    if (schema == nullptr || schema->AsNumber() == nullptr || *schema->AsNumber() != 1.0 || !id.has_value() ||
        controls == nullptr || controls->AsObject() == nullptr) {
        return false;
    }
    const auto knob = StringField(*controls->AsObject(), "knob");
    const auto toggle = StringField(*controls->AsObject(), "toggle");
    const auto button = StringField(*controls->AsObject(), "button");
    const auto footswitch = StringField(*controls->AsObject(), "footswitch");
    if (!knob.has_value() || !toggle.has_value() || !button.has_value() || !footswitch.has_value() || id->empty()) {
        return false;
    }
    skin_ = {.id = *id, .knob = *knob, .toggle = *toggle, .button = *button, .footswitch = *footswitch};
    RebuildScene();
    return true;
}

void RigEditorViewModel::SetViewport(double width, double height, double dpi_scale) {
    viewport_width_ = std::max(width, 1.0);
    viewport_height_ = std::max(height, 1.0);
    dpi_scale_ = std::isfinite(dpi_scale) && dpi_scale > 0.0 ? dpi_scale : 1.0;
    RebuildScene();
}

const RigEditorScene& RigEditorViewModel::Scene() const noexcept {
    return scene_;
}

std::optional<std::string> RigEditorViewModel::HitTest(EditorPoint point) const {
    for (auto iterator = scene_.nodes.rbegin(); iterator != scene_.nodes.rend(); ++iterator) {
        if (iterator->bounds.Contains(point)) {
            return iterator->instance_id;
        }
    }
    return std::nullopt;
}

bool RigEditorViewModel::SelectAt(EditorPoint point) {
    const auto hit = HitTest(point);
    for (auto& node : scene_.nodes) {
        node.selected = hit.has_value() && node.instance_id == *hit;
    }
    return hit.has_value();
}

bool RigEditorViewModel::BeginDrag(EditorPoint point) {
    const auto hit = HitTest(point);
    if (!hit.has_value()) {
        static_cast<void>(SelectAt(point));
        return false;
    }
    static_cast<void>(SelectAt(point));
    const auto* node = FindSceneNode(*hit);
    if (node == nullptr) {
        return false;
    }
    dragging_instance_id_ = *hit;
    drag_offset_ = {.x = point.x / dpi_scale_ - node->bounds.x / dpi_scale_,
                    .y = point.y / dpi_scale_ - node->bounds.y / dpi_scale_};
    return true;
}

bool RigEditorViewModel::DragTo(EditorPoint point, bool snap_to_grid) {
    if (dragging_instance_id_.empty()) {
        return false;
    }
    const auto* node = FindSceneNode(dragging_instance_id_);
    if (node == nullptr) {
        return false;
    }
    const auto position = PedalPosition{.x = point.x / dpi_scale_ - drag_offset_.x,
                                        .y = point.y / dpi_scale_ - drag_offset_.y};
    if (!layout_.Move(dragging_instance_id_, position, snap_to_grid)) {
        return false;
    }
    dirty_ = true;
    SetFileStatus("Unsaved changes");
    RebuildScene();
    return true;
}

void RigEditorViewModel::EndDrag() noexcept {
    dragging_instance_id_.clear();
}

void RigEditorViewModel::ToggleTheme() noexcept {
    SetThemeMode(!dark_theme_);
}

bool RigEditorViewModel::IsDragging() const noexcept {
    return !dragging_instance_id_.empty();
}

std::string_view RigEditorViewModel::AudioStatus() const noexcept {
    return scene_.audio_status;
}

void RigEditorViewModel::RebuildScene() {
    scene_.viewport_width = viewport_width_;
    scene_.viewport_height = viewport_height_;
    scene_.dpi_scale = dpi_scale_;
    scene_.theme = theme_;
    scene_.skin = skin_;
    scene_.audio_status = "ASIO backend not configured/available";
    scene_.file_status = file_status_;
    scene_.source_path = source_path_.has_value() ? source_path_->string() : std::string{};
    scene_.dirty = dirty_;
    scene_.nodes.clear();
    scene_.connections.clear();
    constexpr double kNodeWidth = 196.0;
    constexpr double kNodeHeight = 126.0;
    for (const auto& node : preset_.nodes) {
        const auto position = layout_.Find(node.instance_id).value_or(PedalPosition{});
        scene_.nodes.push_back({.instance_id = node.instance_id,
                                .module_id = node.module_id,
                                .bounds = {.x = position.x * dpi_scale_,
                                           .y = position.y * dpi_scale_,
                                           .width = kNodeWidth * dpi_scale_,
                                           .height = kNodeHeight * dpi_scale_},
                                .selected = node.instance_id == dragging_instance_id_,
                                .unresolved = !IsKnownModule(node.module_id),
                                .diagnostic = !IsKnownModule(node.module_id) ? "Missing module asset" : std::string{}});
    }
    for (const auto& connection : preset_.connections) {
        const auto* source = FindSceneNode(connection.source_instance_id);
        const auto* destination = FindSceneNode(connection.destination_instance_id);
        if (source != nullptr && destination != nullptr) {
            scene_.connections.push_back({.source_instance_id = connection.source_instance_id,
                                          .destination_instance_id = connection.destination_instance_id,
                                          .source = {.x = source->bounds.x + source->bounds.width,
                                                      .y = source->bounds.y + source->bounds.height * 0.5},
                                          .destination = {.x = destination->bounds.x,
                                                          .y = destination->bounds.y + destination->bounds.height * 0.5}});
        }
    }
}

void RigEditorViewModel::SetFileStatus(std::string status) {
    file_status_ = std::move(status);
    scene_.file_status = file_status_;
    scene_.dirty = dirty_;
}

std::optional<std::filesystem::path> RigEditorViewModel::NormalizePresetPath(
    const std::filesystem::path& source) {
    auto extension = source.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (source.empty() || extension != ".srig") {
        return std::nullopt;
    }
    std::error_code error;
    auto normalized = std::filesystem::absolute(source, error);
    if (error) {
        return std::nullopt;
    }
    return normalized.lexically_normal();
}

std::optional<EditorFileError> RigEditorViewModel::MapLoadError(PresetFileError error) {
    switch (error) {
    case PresetFileError::CannotRead:
        return EditorFileError::CannotRead;
    case PresetFileError::InvalidJson:
        return EditorFileError::InvalidJson;
    case PresetFileError::InvalidSchema:
    case PresetFileError::InvalidPreset:
    case PresetFileError::CannotWrite:
        return EditorFileError::InvalidSchema;
    }
    return EditorFileError::InvalidSchema;
}

bool RigEditorViewModel::HasInvalidTopology(const RigPreset& preset) {
    std::map<std::string, std::vector<std::string>, std::less<>> graph;
    for (const auto& node : preset.nodes) {
        graph[node.instance_id];
    }
    for (const auto& connection : preset.connections) {
        graph[connection.source_instance_id].push_back(connection.destination_instance_id);
    }
    std::map<std::string, int, std::less<>> colors;
    const auto visit = [&graph, &colors](const auto& self, const std::string& node) -> bool {
        colors[node] = 1;
        for (const auto& destination : graph[node]) {
            if (colors[destination] == 1 || (colors[destination] == 0 && self(self, destination))) {
                return true;
            }
        }
        colors[node] = 2;
        return false;
    };
    for (const auto& [node, unused] : graph) {
        static_cast<void>(unused);
        if (colors[node] == 0 && visit(visit, node)) {
            return true;
        }
    }
    return false;
}

bool RigEditorViewModel::IsKnownModule(std::string_view module_id) noexcept {
    return module_id == "org.mumu-dada.solorig.drive" ||
           module_id == "org.mumu-dada.solorig.digital-delay";
}

double RigEditorViewModel::GridSize() const noexcept {
    const auto metric = theme_.metrics.find("gridSize");
    return metric != theme_.metrics.end() && metric->second > 0.0 ? metric->second : 16.0;
}

const RigEditorNode* RigEditorViewModel::FindSceneNode(std::string_view instance_id) const noexcept {
    const auto iterator = std::ranges::find_if(scene_.nodes, [instance_id](const RigEditorNode& node) {
        return node.instance_id == instance_id;
    });
    return iterator == scene_.nodes.end() ? nullptr : &*iterator;
}

} // namespace solo_rig
