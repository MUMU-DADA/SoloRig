#include "test_framework.hpp"

#include "content/preset.hpp"
#include "ui/rig_editor.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path SourcePath(std::string_view relative) {
#ifdef SOLO_RIG_SOURCE_DIRECTORY
    return std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / relative;
#else
    return std::filesystem::path(relative);
#endif
}

} // namespace

TEST_CASE("RigEditor builds the default Drive and Delay scene with honest audio status") {
    solo_rig::RigEditorViewModel editor;
    REQUIRE(editor.Scene().nodes.size() == 2U);
    REQUIRE(editor.Scene().connections.size() == 1U);
    REQUIRE(editor.Scene().nodes[0].module_id == "org.mumu-dada.solorig.drive");
    REQUIRE(editor.Scene().nodes[1].module_id == "org.mumu-dada.solorig.digital-delay");
    REQUIRE(editor.AudioStatus() == "ASIO backend not configured/available");
}

TEST_CASE("RigEditor supports hit selection drag snapping and DPI scaling") {
    solo_rig::RigEditorViewModel editor;
    editor.SetViewport(1600.0, 900.0, 2.0);
    const auto initial = editor.Scene().nodes.front().bounds;
    REQUIRE(initial.x == 128.0);
    REQUIRE(initial.width == 392.0);
    REQUIRE(editor.SelectAt({.x = initial.x + 20.0, .y = initial.y + 20.0}));
    REQUIRE(editor.Scene().nodes.front().selected);
    REQUIRE(editor.BeginDrag({.x = initial.x + 20.0, .y = initial.y + 20.0}));
    REQUIRE(editor.DragTo({.x = 700.0, .y = 500.0}, true));
    editor.EndDrag();
    const auto exported = editor.ExportPreset();
    REQUIRE(exported.nodes.front().layout_x >= 0.0);
    REQUIRE(std::fmod(exported.nodes.front().layout_x, 16.0) == 0.0);
    REQUIRE(std::fmod(exported.nodes.front().layout_y, 16.0) == 0.0);
    REQUIRE(editor.HitTest({.x = initial.x + 20.0, .y = initial.y + 20.0}) != exported.nodes.front().instance_id);
}

TEST_CASE("RigEditor toggles themes without losing layout and falls back on bad resources") {
    solo_rig::RigEditorViewModel editor;
    const auto before = editor.ExportPreset().nodes.front();
    editor.ToggleTheme();
    REQUIRE(!editor.IsDarkTheme());
    REQUIRE(editor.Scene().theme.id == "solorig.light");
    REQUIRE(editor.ExportPreset().nodes.front().layout_x == before.layout_x);
    REQUIRE(editor.LoadThemeFile(SourcePath("resources/themes/dark.json")));
    REQUIRE(editor.IsDarkTheme());
    REQUIRE(editor.IsDirty());
    REQUIRE(!editor.LoadThemeFile(SourcePath("resources/themes/missing.json")));
    REQUIRE(editor.Scene().theme.id == "solorig.dark");
    REQUIRE(!editor.LoadSkinFile(SourcePath("resources/skins/missing.json")));
    REQUIRE(editor.Scene().skin.id == "solorig.default-pedal");
    const auto damaged_skin = std::filesystem::temp_directory_path() / "solorig-damaged-pedal.json";
    {
        std::ofstream output(damaged_skin, std::ios::binary | std::ios::trunc);
        output << "{not-json";
    }
    REQUIRE(!editor.LoadSkinFile(damaged_skin));
    std::error_code cleanup_error;
    std::filesystem::remove(damaged_skin, cleanup_error);
    REQUIRE(editor.Scene().skin.id == "solorig.default-pedal");
    REQUIRE(editor.LoadSkinFile(SourcePath("resources/skins/default-pedal.json")));
    REQUIRE(editor.Scene().skin.footswitch == "standard-footswitch");
}

TEST_CASE("RigEditor exports a layout that round-trips through the preset schema") {
    solo_rig::RigEditorViewModel editor;
    REQUIRE(editor.BeginDrag({.x = 70.0, .y = 160.0}));
    REQUIRE(editor.DragTo({.x = 200.0, .y = 224.0}, false));
    editor.EndDrag();
    const auto exported = editor.ExportPreset();
    const auto serialized = solo_rig::SerializePreset(exported);
    const auto restored = solo_rig::DeserializePreset(serialized);
    REQUIRE(restored.has_value());
    REQUIRE(restored->nodes.size() == exported.nodes.size());
    REQUIRE(restored->nodes.front().layout_x == exported.nodes.front().layout_x);
    REQUIRE(restored->nodes.front().layout_y == exported.nodes.front().layout_y);
    REQUIRE(restored->connections.size() == 1U);
}

TEST_CASE("RigEditor saves and loads the edited preset with normalized source and clean state") {
    const auto path = std::filesystem::temp_directory_path() / "solorig-editor-roundtrip.srig";
    std::error_code cleanup_error;
    std::filesystem::remove(path, cleanup_error);
    solo_rig::RigEditorViewModel editor;
    REQUIRE(!editor.IsDirty());
    REQUIRE(editor.BeginDrag({.x = 70.0, .y = 160.0}));
    REQUIRE(editor.DragTo({.x = 210.0, .y = 232.0}, false));
    editor.EndDrag();
    REQUIRE(editor.SetNodeParameter("drive-1", "drive", 7.0F));
    editor.SetThemeMode(false);
    REQUIRE(editor.IsDirty());
    const auto saved = editor.SaveTo(path);
    REQUIRE(saved.success);
    REQUIRE(!editor.IsDirty());
    REQUIRE(editor.SourcePath().has_value());
    REQUIRE(editor.SourcePath()->is_absolute());
    REQUIRE(editor.SourcePath()->lexically_normal() == std::filesystem::absolute(path).lexically_normal());
    REQUIRE(editor.Scene().source_path == editor.SourcePath()->string());
    REQUIRE(editor.Scene().dirty == false);

    solo_rig::RigEditorViewModel restored;
    const auto loaded = restored.LoadFrom(path);
    REQUIRE(loaded.success);
    REQUIRE(!loaded.warning);
    REQUIRE(!restored.IsDirty());
    REQUIRE(restored.ExportPreset().nodes.front().layout_x == editor.ExportPreset().nodes.front().layout_x);
    REQUIRE(restored.ExportPreset().nodes.front().parameter_values.at("drive") == 7.0F);
    REQUIRE(restored.Scene().theme.id == editor.Scene().theme.id);
    REQUIRE(restored.Scene().theme.colors == editor.Scene().theme.colors);
    REQUIRE(restored.Scene().theme.metrics == editor.Scene().theme.metrics);
    REQUIRE(restored.Scene().source_path == restored.SourcePath()->string());
    REQUIRE(restored.FileStatus().find("Loaded:") == 0U);
    std::filesystem::remove(path, cleanup_error);
}

TEST_CASE("RigEditor marks theme and layout changes dirty and clears after SaveCurrent") {
    const auto path = std::filesystem::temp_directory_path() / "solorig-editor-dirty.srig";
    std::error_code cleanup_error;
    std::filesystem::remove(path, cleanup_error);
    solo_rig::RigEditorViewModel editor;
    REQUIRE(editor.SaveTo(path).success);
    REQUIRE(!editor.IsDirty());
    editor.ToggleTheme();
    REQUIRE(editor.IsDirty());
    REQUIRE(editor.Scene().dirty);
    REQUIRE(editor.FileStatus() == "Unsaved changes");
    REQUIRE(editor.SaveCurrent().success);
    REQUIRE(!editor.IsDirty());
    REQUIRE(!editor.Scene().dirty);
    std::filesystem::remove(path, cleanup_error);
}

TEST_CASE("RigEditor rejects failed loads without changing the current canvas") {
    const auto directory = std::filesystem::temp_directory_path();
    const auto valid_path = directory / "solorig-editor-valid.srig";
    const auto corrupt_path = directory / "solorig-editor-corrupt.srig";
    const auto schema_path = directory / "solorig-editor-schema.srig";
    const auto topology_path = directory / "solorig-editor-topology.srig";
    std::error_code cleanup_error;
    solo_rig::RigEditorViewModel editor;
    REQUIRE(editor.SaveTo(valid_path).success);
    const auto before = editor.ExportPreset();
    const auto before_source = editor.SourcePath();
    {
        std::ofstream output(corrupt_path, std::ios::binary | std::ios::trunc);
        output << "{not-json";
    }
    {
        std::ofstream output(schema_path, std::ios::binary | std::ios::trunc);
        output << "{\"schemaVersion\":99,\"name\":\"Bad\",\"nodes\":[],\"connections\":[]}";
    }
    {
        std::ofstream output(topology_path, std::ios::binary | std::ios::trunc);
        output << "{\"schemaVersion\":2,\"presetId\":\"cycle\",\"name\":\"Cycle\",\"nodes\":["
                   "{\"instanceId\":\"a\",\"moduleId\":\"org.mumu-dada.solorig.drive\",\"x\":0,\"y\":0,\"parameters\":{}},"
                   "{\"instanceId\":\"b\",\"moduleId\":\"org.mumu-dada.solorig.digital-delay\",\"x\":1,\"y\":0,\"parameters\":{}}],"
                   "\"connections\":[{\"source\":\"a\",\"destination\":\"b\"},{\"source\":\"b\",\"destination\":\"a\"}]}";
    }
    for (const auto& path : {corrupt_path, schema_path, topology_path, directory / "missing.srig"}) {
        const auto result = editor.LoadFrom(path);
        REQUIRE(!result.success);
        REQUIRE(editor.ExportPreset().nodes.size() == before.nodes.size());
        REQUIRE(editor.ExportPreset().nodes.front().instance_id == before.nodes.front().instance_id);
        REQUIRE(editor.SourcePath() == before_source);
    }
    REQUIRE(!editor.LoadFrom(directory / "not-a-preset.json").success);
    for (const auto& path : {valid_path, corrupt_path, schema_path, topology_path}) {
        std::filesystem::remove(path, cleanup_error);
    }
}

TEST_CASE("RigEditor loads missing modules as visible placeholder nodes with a warning") {
    const auto path = std::filesystem::temp_directory_path() / "solorig-editor-missing-module.srig";
    std::error_code cleanup_error;
    std::filesystem::remove(path, cleanup_error);
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "{\"schemaVersion\":2,\"presetId\":\"missing\",\"name\":\"Missing\",\"nodes\":["
                   "{\"instanceId\":\"nam-1\",\"moduleId\":\"external.missing-nam\",\"x\":32,\"y\":48,\"parameters\":{}}],"
                   "\"connections\":[]}";
    }
    solo_rig::RigEditorViewModel editor;
    const auto result = editor.LoadFrom(path);
    REQUIRE(result.success);
    REQUIRE(result.warning);
    REQUIRE(editor.Scene().nodes.size() == 1U);
    REQUIRE(editor.Scene().nodes.front().unresolved);
    REQUIRE(editor.Scene().nodes.front().diagnostic == "Missing module asset");
    REQUIRE(editor.FileStatus().find("placeholders") != std::string_view::npos);
    std::filesystem::remove(path, cleanup_error);
}
