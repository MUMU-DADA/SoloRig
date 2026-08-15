#include "test_framework.hpp"

#include "content/default_preset.hpp"
#include "content/preset.hpp"
#include "ui/rig_editor.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <algorithm>
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
    const auto shared = solo_rig::DefaultRigPreset();
    solo_rig::RigEditorViewModel editor;
    REQUIRE(editor.ExportPreset().nodes.size() == shared.nodes.size());
    REQUIRE(editor.ExportPreset().connections.size() == shared.connections.size());
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
    REQUIRE(editor.SetMidiBindings({{.message_type = solo_rig::MidiMessageType::ControlChange,
                                     .channel = 0,
                                     .number = 11,
                                     .target_kind = solo_rig::MidiTargetKind::Parameter,
                                     .target_id = "drive-1.drive",
                                     .minimum = 0.0F,
                                     .maximum = 1.0F}}));
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
    REQUIRE(restored.MidiBindings().has_value());
    REQUIRE(restored.MidiBindings()->size() == 1U);
    REQUIRE(restored.MidiBindings()->front().target_id == "drive-1.drive");
    REQUIRE(restored.Scene().theme.id == editor.Scene().theme.id);
    REQUIRE(restored.Scene().theme.colors == editor.Scene().theme.colors);
    REQUIRE(restored.Scene().theme.metrics == editor.Scene().theme.metrics);
    REQUIRE(restored.Scene().source_path == restored.SourcePath()->string());
    REQUIRE(restored.FileStatus().find("Loaded:") == 0U);
    std::filesystem::remove(path, cleanup_error);
}

TEST_CASE("RigEditor explicitly validates an external module package resource") {
    const auto directory = std::filesystem::temp_directory_path() / "solorig-editor-module-package";
    std::error_code cleanup;
    std::filesystem::remove_all(directory, cleanup);
    std::filesystem::create_directories(directory / "logic");
    {
        std::ofstream manifest(directory / "manifest.json", std::ios::binary | std::ios::trunc);
        manifest << R"({
            "schemaVersion": 1,
            "moduleId": "org.mumu-dada.solorig.editor-package",
            "displayName": "Editor Package",
            "category": "analog-circuit",
            "logicFile": "logic/circuit.json",
            "ports": [
                {"id":"input","direction":"input","maxChannels":1},
                {"id":"output","direction":"output","maxChannels":1}
            ],
            "parameters": []
        })";
        std::ofstream logic(directory / "logic/circuit.json", std::ios::binary | std::ios::trunc);
        logic << R"({
            "schemaVersion": 1,
            "kind": "analog-circuit",
            "solver": "modified-nodal-analysis",
            "components": [
                {"id":"r1","type":"resistor","nodes":["input","output"],"value":1000}
            ]
        })";
    }
    solo_rig::RigEditorViewModel editor;
    const auto imported = editor.ImportResource(directory);
    REQUIRE(imported.success);
    REQUIRE(editor.Scene().resource_status.find("Editor Package") != std::string::npos);
    REQUIRE(editor.IsDirty());
    const auto duplicate_import = editor.ImportResource(directory);
    REQUIRE(!duplicate_import.success);
    REQUIRE(editor.ExportPreset().external_module_packages.size() == 1U);
    REQUIRE(editor.ExportPreset().external_module_packages.front().module_id ==
            "org.mumu-dada.solorig.editor-package");
    REQUIRE(editor.Catalog().size() == 3U);
    REQUIRE(std::ranges::any_of(editor.Catalog(), [](const auto& entry) {
        return entry.module_id == "org.mumu-dada.solorig.editor-package" &&
               entry.default_parameters.empty();
    }));

    const auto preset_path = std::filesystem::temp_directory_path() / "solorig-editor-module-package.srig";
    const auto saved = editor.SaveTo(preset_path);
    REQUIRE(saved.success);
    solo_rig::RigEditorViewModel restored;
    const auto restored_result = restored.LoadFrom(preset_path);
    REQUIRE(restored_result.success);
    REQUIRE(restored.Catalog().size() == 3U);
    REQUIRE(restored.AddNode("org.mumu-dada.solorig.editor-package", "editor-package-1", 600.0, 160.0));
    REQUIRE(!restored.Scene().nodes.back().unresolved);
    std::filesystem::remove(preset_path, cleanup);
    std::filesystem::remove_all(directory, cleanup);
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
    const auto invalid_midi_path = directory / "solorig-editor-invalid-midi.srig";
    std::error_code cleanup_error;
    solo_rig::RigEditorViewModel editor;
    REQUIRE(editor.SaveTo(valid_path).success);
    const auto before = editor.ExportPreset();
    const auto before_source = editor.SourcePath();
    const auto before_dirty = editor.IsDirty();
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
    {
        std::ofstream output(invalid_midi_path, std::ios::binary | std::ios::trunc);
        output << "{\"schemaVersion\":2,\"presetId\":\"invalid-midi\",\"name\":\"Invalid MIDI\","
                   "\"nodes\":[],\"connections\":[],\"midiBindings\":[{\"messageType\":\"controlChange\","
                   "\"channel\":0,\"number\":11,\"targetKind\":\"parameter\","
                   "\"targetId\":\"drive-1.drive\",\"minimum\":1.0,\"maximum\":0.0}]}";
    }
    for (const auto& path : {corrupt_path, schema_path, topology_path, invalid_midi_path,
                             directory / "missing.srig"}) {
        const auto result = editor.LoadFrom(path);
        REQUIRE(!result.success);
        REQUIRE(editor.ExportPreset().nodes.size() == before.nodes.size());
        REQUIRE(editor.ExportPreset().nodes.front().instance_id == before.nodes.front().instance_id);
        REQUIRE(editor.SourcePath() == before_source);
        REQUIRE(editor.IsDirty() == before_dirty);
    }
    REQUIRE(!editor.LoadFrom(directory / "not-a-preset.json").success);
    for (const auto& path : {valid_path, corrupt_path, schema_path, topology_path, invalid_midi_path}) {
        std::filesystem::remove(path, cleanup_error);
    }
}

TEST_CASE("RigEditor restores the built-in dark theme for legacy presets without theme metadata") {
    const auto directory = std::filesystem::temp_directory_path();
    const auto legacy_path = directory / "solorig-editor-legacy-v2.srig";
    const auto corrupt_path = directory / "solorig-editor-legacy-corrupt.srig";
    std::error_code cleanup_error;
    std::filesystem::remove(legacy_path, cleanup_error);
    std::filesystem::remove(corrupt_path, cleanup_error);

    auto legacy = solo_rig::RigEditorViewModel::DefaultEditorPreset();
    legacy.theme.reset();
    REQUIRE(!solo_rig::SavePreset(legacy_path, legacy).has_value());
    {
        std::ofstream output(corrupt_path, std::ios::binary | std::ios::trunc);
        output << "{not-json";
    }

    solo_rig::RigEditorViewModel editor;
    editor.SetThemeMode(false);
    REQUIRE(!editor.IsDarkTheme());
    const auto loaded = editor.LoadFrom(legacy_path);
    REQUIRE(loaded.success);
    REQUIRE(!loaded.warning);
    REQUIRE(editor.IsDarkTheme());
    REQUIRE(editor.Scene().theme.id == "solorig.dark");
    REQUIRE(!editor.IsDirty());

    const auto before_failed_load = editor.ExportPreset();
    const auto before_source = editor.SourcePath();
    const auto before_dirty = editor.IsDirty();
    const auto failed = editor.LoadFrom(corrupt_path);
    REQUIRE(!failed.success);
    REQUIRE(editor.ExportPreset().theme.has_value());
    REQUIRE(before_failed_load.theme.has_value());
    REQUIRE(editor.ExportPreset().theme->id == before_failed_load.theme->id);
    REQUIRE(editor.ExportPreset().theme->colors == before_failed_load.theme->colors);
    REQUIRE(editor.ExportPreset().theme->metrics == before_failed_load.theme->metrics);
    REQUIRE(editor.SourcePath() == before_source);
    REQUIRE(editor.IsDirty() == before_dirty);
    REQUIRE(editor.Scene().theme.id == "solorig.dark");

    std::filesystem::remove(legacy_path, cleanup_error);
    std::filesystem::remove(corrupt_path, cleanup_error);
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

TEST_CASE("RigEditor exposes a catalog and transactional node editing") {
    const auto catalog = solo_rig::RigEditorViewModel::ModuleCatalog();
    REQUIRE(catalog.size() >= 2U);
    REQUIRE(std::ranges::any_of(catalog, [](const auto& entry) {
        return entry.module_id == "org.mumu-dada.solorig.drive";
    }));
    solo_rig::RigEditorViewModel editor;
    const auto before = editor.ExportPreset();
    REQUIRE(editor.AddNode("org.mumu-dada.solorig.drive", "drive-2", 600.0, 160.0));
    REQUIRE(editor.ExportPreset().nodes.size() == before.nodes.size() + 1U);
    REQUIRE(editor.ExportPreset().nodes.back().parameter_values.at("drive") == 4.0F);
    const auto after_add = editor.ExportPreset();
    REQUIRE(!editor.AddNode("unknown.module", "bad", 0.0, 0.0));
    REQUIRE(!editor.AddNode("org.mumu-dada.solorig.drive", "drive-2", 0.0, 0.0));
    REQUIRE(editor.ExportPreset().nodes.size() == after_add.nodes.size());
    REQUIRE(editor.DuplicateNode("drive-2", "drive-3"));
    REQUIRE(editor.DeleteNode("drive-3"));
    REQUIRE(!editor.DeleteNode("missing"));
    REQUIRE(editor.ExportPreset().nodes.size() == after_add.nodes.size());
}

TEST_CASE("RigEditor connection edits validate topology and preserve state on failure") {
    solo_rig::RigEditorViewModel editor;
    REQUIRE(editor.AddNode("org.mumu-dada.solorig.drive", "parallel-1", 600.0, 320.0));
    const auto before = editor.ExportPreset();
    REQUIRE(editor.ConnectNodes("drive-1", "parallel-1"));
    REQUIRE(editor.ExportPreset().connections.size() == before.connections.size() + 1U);
    const auto connected = editor.ExportPreset();
    REQUIRE(!editor.ConnectNodes("drive-1", "parallel-1"));
    REQUIRE(!editor.ConnectNodes("parallel-1", "missing"));
    REQUIRE(!editor.ConnectNodes("parallel-1", "drive-1"));
    REQUIRE(editor.ExportPreset().connections.size() == connected.connections.size());
    REQUIRE(editor.DisconnectNodes("drive-1", "parallel-1"));
    REQUIRE(!editor.DisconnectNodes("drive-1", "parallel-1"));
}

TEST_CASE("RigEditor exposes deterministic performance and explicit resource recognition") {
    solo_rig::RigEditorViewModel editor;
    REQUIRE(editor.Scene().performance.total_latency_samples > 0U);
    REQUIRE(editor.Scene().performance.estimated_cpu_percent > 0.0);
    REQUIRE(editor.Scene().nodes[0].declared_latency_samples == 0U);
    REQUIRE(editor.Scene().nodes[0].estimated_cpu_percent > 0.0);
    REQUIRE(!editor.Scene().performance.measurement_available);
    REQUIRE(editor.SetMeasuredPerformance("drive-1", 7U, 3.5, 4.0));
    REQUIRE(editor.Scene().nodes[0].declared_latency_samples == 7U);
    REQUIRE(editor.Scene().nodes[0].measurement_available);
    REQUIRE(editor.Scene().nodes[0].measured_cpu_percent == 3.5);
    REQUIRE(editor.Scene().performance.measured_node_count == 1U);
    REQUIRE(!editor.Scene().performance.measurement_available);
    REQUIRE(editor.SetMeasuredPerformance("delay-1", 96U, 2.5, 3.0));
    REQUIRE(editor.Scene().performance.measured_node_count == 2U);
    REQUIRE(editor.Scene().performance.measurement_available);
    editor.ClearMeasuredPerformance();
    REQUIRE(!editor.Scene().performance.measurement_available);
    const auto before = editor.Scene().resource_status;
    const auto missing = editor.ImportResource(std::filesystem::temp_directory_path() / "missing.nam");
    REQUIRE(!missing.success);
    REQUIRE(editor.Scene().resource_status == before);
    const auto path = std::filesystem::temp_directory_path() / "solorig-test-model.nam";
    std::error_code copy_error;
    std::filesystem::copy_file(
        SourcePath("third_party/NeuralAmpModelerCore/example_models/wavenet.nam"),
        path,
        std::filesystem::copy_options::overwrite_existing,
        copy_error);
    REQUIRE(!copy_error);
    const auto imported = editor.ImportResource(path);
    REQUIRE(imported.success);
    REQUIRE(editor.Scene().resource_status.find("NAM") != std::string::npos);
    REQUIRE(editor.ExportPreset().external_module_packages.size() == 1U);
    REQUIRE(editor.Catalog().size() == 3U);
    const auto preset_path = std::filesystem::temp_directory_path() / "solorig-direct-model.srig";
    REQUIRE(editor.SaveTo(preset_path).success);
    solo_rig::RigEditorViewModel restored;
    REQUIRE(restored.LoadFrom(preset_path).success);
    REQUIRE(restored.Catalog().size() == 3U);
    REQUIRE(std::ranges::any_of(restored.Catalog(), [](const auto& entry) {
        return entry.category == "NAM";
    }));
    std::error_code cleanup;
    std::filesystem::remove(path, cleanup);
    std::filesystem::remove(preset_path, cleanup);
}
