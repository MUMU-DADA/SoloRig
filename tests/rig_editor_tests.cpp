#include "test_framework.hpp"

#include "content/preset.hpp"
#include "ui/rig_editor.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>

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
