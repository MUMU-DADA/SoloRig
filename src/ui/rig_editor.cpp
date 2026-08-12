#include "ui/rig_editor.hpp"

#include "content/json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <ranges>

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
    SetPreset(std::move(preset));
}

void RigEditorViewModel::SetPreset(RigPreset preset) {
    if (preset.name.empty()) {
        preset = DefaultEditorPreset();
    }
    preset_ = std::move(preset);
    layout_ = PedalLayout(GridSize());
    for (const auto& node : preset_.nodes) {
        const auto position = (std::isfinite(node.layout_x) && std::isfinite(node.layout_y))
            ? PedalPosition{node.layout_x, node.layout_y}
            : PedalPosition{};
        static_cast<void>(layout_.Add(node.instance_id, position));
    }
    dragging_instance_id_.clear();
    RebuildScene();
}

RigPreset RigEditorViewModel::ExportPreset() const {
    auto result = preset_;
    for (auto& node : result.nodes) {
        if (const auto position = layout_.Find(node.instance_id); position.has_value()) {
            node.layout_x = position->x;
            node.layout_y = position->y;
        }
    }
    return result;
}

bool RigEditorViewModel::LoadThemeFile(const std::filesystem::path& source) {
    const auto loaded = solo_rig::LoadThemeFile(source);
    if (std::holds_alternative<ThemeDefinition>(loaded)) {
        SetTheme(std::get<ThemeDefinition>(loaded));
        return true;
    }
    // Keep the current valid theme when an external resource is missing or
    // malformed; the scene remains renderable with built-in defaults.
    return false;
}

void RigEditorViewModel::SetThemeMode(bool dark) {
    dark_theme_ = dark;
    theme_ = dark ? DefaultDarkTheme() : DefaultLightTheme();
    RebuildScene();
}

void RigEditorViewModel::SetTheme(ThemeDefinition theme) {
    if (theme.id.empty() || theme.colors.empty()) {
        return;
    }
    dark_theme_ = theme.id.find("dark") != std::string::npos;
    theme_ = std::move(theme);
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
                                .selected = node.instance_id == dragging_instance_id_});
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
