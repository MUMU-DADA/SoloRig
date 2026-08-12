#pragma once

#include "content/theme.hpp"
#include "content/preset.hpp"
#include "ui/pedal_layout.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace solo_rig {

struct EditorPoint {
    double x{};
    double y{};
};

struct EditorRect {
    double x{};
    double y{};
    double width{};
    double height{};

    [[nodiscard]] bool Contains(EditorPoint point) const noexcept {
        return point.x >= x && point.x <= x + width && point.y >= y && point.y <= y + height;
    }
};

struct PedalSkinDefinition {
    std::string id;
    std::string knob;
    std::string toggle;
    std::string button;
    std::string footswitch;
};

[[nodiscard]] const PedalSkinDefinition& DefaultPedalSkin();

struct RigEditorNode {
    std::string instance_id;
    std::string module_id;
    EditorRect bounds;
    bool selected{};
};

struct RigEditorConnection {
    std::string source_instance_id;
    std::string destination_instance_id;
    EditorPoint source;
    EditorPoint destination;
};

struct RigEditorScene {
    double viewport_width{};
    double viewport_height{};
    double dpi_scale{1.0};
    ThemeDefinition theme;
    PedalSkinDefinition skin;
    std::vector<RigEditorNode> nodes;
    std::vector<RigEditorConnection> connections;
    std::string audio_status;
};

class RigEditorViewModel {
public:
    explicit RigEditorViewModel(RigPreset preset = DefaultEditorPreset());

    [[nodiscard]] static RigPreset DefaultEditorPreset();
    void SetPreset(RigPreset preset);
    [[nodiscard]] RigPreset ExportPreset() const;

    [[nodiscard]] bool LoadThemeFile(const std::filesystem::path& source);
    void SetThemeMode(bool dark);
    void SetTheme(ThemeDefinition theme);
    [[nodiscard]] bool IsDarkTheme() const noexcept;
    [[nodiscard]] bool LoadSkinFile(const std::filesystem::path& source);

    void SetViewport(double width, double height, double dpi_scale);
    [[nodiscard]] const RigEditorScene& Scene() const noexcept;
    [[nodiscard]] std::optional<std::string> HitTest(EditorPoint point) const;
    [[nodiscard]] bool SelectAt(EditorPoint point);
    [[nodiscard]] bool BeginDrag(EditorPoint point);
    [[nodiscard]] bool DragTo(EditorPoint point, bool snap_to_grid = true);
    void EndDrag() noexcept;
    void ToggleTheme() noexcept;
    [[nodiscard]] bool IsDragging() const noexcept;
    [[nodiscard]] std::string_view AudioStatus() const noexcept;

private:
    void RebuildScene();
    [[nodiscard]] double GridSize() const noexcept;
    [[nodiscard]] const RigEditorNode* FindSceneNode(std::string_view instance_id) const noexcept;

    RigPreset preset_;
    PedalLayout layout_;
    ThemeDefinition theme_;
    PedalSkinDefinition skin_;
    bool dark_theme_{true};
    double viewport_width_{1280.0};
    double viewport_height_{720.0};
    double dpi_scale_{1.0};
    std::string dragging_instance_id_;
    EditorPoint drag_offset_{};
    RigEditorScene scene_;
};

} // namespace solo_rig
