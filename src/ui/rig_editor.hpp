#pragma once

#include "content/theme.hpp"
#include "content/module_package.hpp"
#include "content/preset.hpp"
#include "ui/pedal_layout.hpp"

#include <filesystem>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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
    // Optional image assets are resolved relative to the skin JSON file and
    // remain empty for the built-in geometry skin.
    std::optional<std::filesystem::path> knob_image;
    std::optional<std::filesystem::path> toggle_image;
    std::optional<std::filesystem::path> button_image;
    std::optional<std::filesystem::path> footswitch_image;
};

[[nodiscard]] const PedalSkinDefinition& DefaultPedalSkin();

struct RigEditorNode {
    std::string instance_id;
    std::string module_id;
    PedalSkinDefinition skin;
    EditorRect bounds;
    bool selected{};
    bool unresolved{};
    bool bypassed{};
    std::string diagnostic;
    uint32_t declared_latency_samples{};
    double estimated_cpu_percent{};
    double measured_cpu_percent{};
    double peak_cpu_percent{};
    bool measurement_available{};
};

struct RigEditorConnection {
    std::string source_instance_id;
    std::string destination_instance_id;
    EditorPoint source;
    EditorPoint destination;
};

struct ModuleCatalogEntry {
    std::string module_id;
    std::string display_name;
    std::string category;
    std::string description;
    std::vector<std::pair<std::string, float>> default_parameters;
    std::vector<EffectParameter> parameter_descriptors;
    uint32_t latency_samples{};
    double estimated_cpu_percent{};
};

struct RigEditorPerformance {
    uint32_t total_latency_samples{};
    double estimated_cpu_percent{};
    double measured_cpu_percent{};
    double peak_cpu_percent{};
    uint32_t measured_node_count{};
    bool measurement_available{};
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
    std::string file_status;
    std::string source_path;
    std::string resource_status;
    RigEditorPerformance performance;
    bool dirty{};
};

enum class EditorFileError {
    InvalidExtension,
    CannotRead,
    InvalidJson,
    InvalidSchema,
    InvalidTopology,
    CannotWrite,
};

struct EditorFileResult {
    bool success{};
    bool warning{};
    std::optional<EditorFileError> error;
    std::string message;
};

class RigEditorViewModel {
public:
    explicit RigEditorViewModel(RigPreset preset = DefaultEditorPreset());

    [[nodiscard]] static RigPreset DefaultEditorPreset();
    void SetPreset(RigPreset preset);
    // Control-thread transaction rollback used when a newly loaded document
    // cannot be prepared by the audio backend. Restores the document metadata
    // as well as the preset so the editor and runtime describe the same graph.
    void RestoreDocument(RigPreset preset,
                         std::optional<std::filesystem::path> source,
                         bool dirty,
                         std::string status);
    void NewPreset();
    [[nodiscard]] RigPreset ExportPreset() const;
    [[nodiscard]] EditorFileResult SaveTo(const std::filesystem::path& destination);
    [[nodiscard]] EditorFileResult LoadFrom(const std::filesystem::path& source);
    [[nodiscard]] EditorFileResult SaveCurrent();
    [[nodiscard]] bool SetNodeParameter(std::string_view instance_id,
                                         std::string_view parameter_id,
                                         float value,
                                         bool mark_dirty = true);
    [[nodiscard]] bool SetNodeBypassed(std::string_view instance_id, bool bypassed);
    // Control-thread host bridge for measurements captured by a stopped
    // RigRuntime. These diagnostics never become part of the saved preset.
    [[nodiscard]] bool SetMeasuredPerformance(std::string_view instance_id,
                                               uint32_t declared_latency_samples,
                                               double measured_cpu_percent,
                                               double peak_cpu_percent);
    void ClearMeasuredPerformance();
    [[nodiscard]] std::optional<std::vector<MidiBinding>> MidiBindings() const;
    [[nodiscard]] bool SetMidiBindings(std::vector<MidiBinding> bindings);
    [[nodiscard]] bool IsDirty() const noexcept;
    [[nodiscard]] std::optional<std::filesystem::path> SourcePath() const;
    [[nodiscard]] std::string_view FileStatus() const noexcept;

    // User initiated theme loads are dirty by default. Startup resource
    // loading may explicitly pass false so a new document remains clean.
    [[nodiscard]] bool LoadThemeFile(const std::filesystem::path& source, bool mark_dirty = true);
    void SetThemeMode(bool dark, bool mark_dirty = true);
    void SetTheme(ThemeDefinition theme, bool mark_dirty = true);
    [[nodiscard]] bool IsDarkTheme() const noexcept;
    [[nodiscard]] bool LoadSkinFile(const std::filesystem::path& source);

    [[nodiscard]] static std::vector<ModuleCatalogEntry> ModuleCatalog();
    // Includes built-ins and all successfully imported package descriptors.
    // The returned storage belongs to this editor and remains valid until the
    // next document/package operation.
    [[nodiscard]] const std::vector<ModuleCatalogEntry>& Catalog() const noexcept;
    [[nodiscard]] bool AddNode(std::string_view module_id, std::string_view instance_id,
                               double layout_x, double layout_y);
    [[nodiscard]] bool DeleteNode(std::string_view instance_id);
    [[nodiscard]] bool DuplicateNode(std::string_view source_instance_id, std::string_view new_instance_id);
    [[nodiscard]] bool ConnectNodes(std::string_view source_instance_id, std::string_view destination_instance_id);
    [[nodiscard]] bool DisconnectNodes(std::string_view source_instance_id, std::string_view destination_instance_id);
    [[nodiscard]] EditorFileResult ImportResource(const std::filesystem::path& source);

    void SetViewport(double width, double height, double dpi_scale);
    [[nodiscard]] const RigEditorScene& Scene() const noexcept;
    [[nodiscard]] std::optional<std::string> HitTest(EditorPoint point) const;
    [[nodiscard]] bool SelectAt(EditorPoint point);
    void ClearSelection() noexcept;
    [[nodiscard]] bool BeginDrag(EditorPoint point);
    [[nodiscard]] bool DragTo(EditorPoint point, bool snap_to_grid = true);
    void EndDrag() noexcept;
    void ToggleTheme() noexcept;
    [[nodiscard]] bool IsDragging() const noexcept;
    [[nodiscard]] std::string_view AudioStatus() const noexcept;
    void SetAudioStatus(std::string status);

private:
    void RebuildScene();
    void RebuildImportedSkins();
    void ApplyPreset(RigPreset preset,
                     std::optional<std::filesystem::path> source,
                     bool dirty,
                     std::map<std::string, ModulePackage, std::less<>> packages = {});
    [[nodiscard]] std::optional<std::map<std::string, ModulePackage, std::less<>>>
    LoadExternalPackages(RigPreset& preset,
                         const std::filesystem::path& base_directory,
                         std::string& diagnostic) const;
    void RebuildCatalog();
    void SetFileStatus(std::string status);
    [[nodiscard]] static std::optional<std::filesystem::path> NormalizePresetPath(
        const std::filesystem::path& source);
    [[nodiscard]] static std::optional<EditorFileError> MapLoadError(PresetFileError error);
    [[nodiscard]] static bool HasInvalidTopology(const RigPreset& preset);
    [[nodiscard]] bool IsKnownModule(std::string_view module_id) const noexcept;
    [[nodiscard]] const ModuleCatalogEntry* FindCatalogEntry(std::string_view module_id) const noexcept;
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
    std::optional<std::filesystem::path> source_path_;
    bool dirty_{};
    std::string file_status_;
    std::string audio_status_{"ASIO backend not configured/available"};
    std::string resource_status_{"No external resource imported"};
    std::map<std::string, ModulePackage, std::less<>> imported_packages_;
    std::map<std::string, PedalSkinDefinition, std::less<>> imported_skins_;
    std::vector<ModuleCatalogEntry> catalog_;
    std::vector<std::string> unresolved_nodes_;
    struct MeasuredNodePerformance {
        uint32_t declared_latency_samples{};
        double measured_cpu_percent{};
        double peak_cpu_percent{};
    };
    std::map<std::string, MeasuredNodePerformance, std::less<>> measured_performance_;
    RigEditorScene scene_;
};

} // namespace solo_rig
