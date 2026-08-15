#include "content/preset.hpp"
#include "content/json.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <variant>

namespace {

solo_rig::RigPreset CreatePreset() {
    return {
        .name = "My Main Rig",
        .nodes = {
            {.instance_id = "drive-1", .module_id = "org.mumu-dada.solorig.drive", .layout_x = 10.0, .layout_y = 20.0,
             .parameter_values = {{"drive", 0.7F}}},
            {.instance_id = "cab-1", .module_id = "org.mumu-dada.solorig.ir", .layout_x = 300.0, .layout_y = 20.0},
        },
        .connections = {{.source_instance_id = "drive-1", .destination_instance_id = "cab-1"}},
    };
}

} // namespace

TEST_CASE("RigPreset serializes and restores graph layout, parameters and routes") {
    const auto serialized = solo_rig::SerializePreset(CreatePreset());
    const auto parsed = solo_rig::ParseJson(serialized);
    const auto restored = solo_rig::DeserializePreset(serialized);

    REQUIRE(parsed.value.has_value());
    REQUIRE(restored.has_value());
    REQUIRE(restored->nodes.size() == 2);
    REQUIRE(restored->nodes[0].parameter_values.at("drive") == 0.7F);
    REQUIRE(restored->connections.size() == 1);
}

TEST_CASE("RigPreset rejects duplicate node identities and dangling routes") {
    auto preset = CreatePreset();
    preset.nodes.push_back(preset.nodes.front());
    preset.connections.push_back({.source_instance_id = "missing", .destination_instance_id = "cab-1"});

    REQUIRE(!solo_rig::ValidatePreset(preset).empty());
}

TEST_CASE("RigPreset saves atomically and reloads from a user selected file") {
    const auto path = std::filesystem::temp_directory_path() / "SoloRigPresetTest.srigpreset";
    std::error_code error;
    std::filesystem::remove(path, error);

    REQUIRE(!solo_rig::SavePreset(path, CreatePreset()).has_value());
    const auto loaded = solo_rig::LoadPreset(path);
    std::filesystem::remove(path, error);

    REQUIRE(std::holds_alternative<solo_rig::RigPreset>(loaded));
    REQUIRE(std::get<solo_rig::RigPreset>(loaded).name == "My Main Rig");
}

TEST_CASE("RigPreset schema version two persists a stable preset identifier") {
    auto preset = CreatePreset();
    preset.preset_id = "main-rig";

    const auto restored = solo_rig::DeserializePreset(solo_rig::SerializePreset(preset));

    REQUIRE(restored.has_value());
    REQUIRE(restored->preset_id == "main-rig");
}

TEST_CASE("RigPreset accepts legacy and additive unknown fields but rejects unknown schema versions") {
    const auto legacy = solo_rig::SerializePreset(CreatePreset());
    const auto with_unknown_field = legacy.substr(0, legacy.size() - 1) + ",\"futureDisplayHint\":true}";
    const auto accepted = solo_rig::DeserializePreset(with_unknown_field);
    const auto path = std::filesystem::temp_directory_path() / "SoloRigUnknownSchemaTest.srigpreset";
    std::error_code error;
    std::filesystem::remove(path, error);
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "{\"schemaVersion\":99,\"name\":\"Future\",\"nodes\":[],\"connections\":[]}";
    }
    const auto rejected = solo_rig::LoadPreset(path);
    std::filesystem::remove(path, error);

    REQUIRE(accepted.has_value());
    REQUIRE(accepted->preset_id.empty());
    REQUIRE(std::holds_alternative<solo_rig::PresetFileError>(rejected));
    REQUIRE(std::get<solo_rig::PresetFileError>(rejected) == solo_rig::PresetFileError::InvalidSchema);
}

TEST_CASE("RigPreset schema-v2 persists optional MIDI bindings and keeps legacy files compatible") {
    auto preset = CreatePreset();
    preset.preset_id = "midi-rig";
    preset.midi_bindings = std::vector<solo_rig::MidiBinding>{
        {.message_type = solo_rig::MidiMessageType::ControlChange,
         .channel = 1,
         .number = 11,
         .target_kind = solo_rig::MidiTargetKind::Parameter,
         .target_id = "drive-1.drive",
         .minimum = 0.0F,
         .maximum = 16.0F},
        {.message_type = solo_rig::MidiMessageType::ProgramChange,
         .channel = 1,
         .number = 5,
         .target_kind = solo_rig::MidiTargetKind::Preset,
         .target_id = "lead",
         .minimum = 0.0F,
         .maximum = 1.0F},
    };
    const auto restored = solo_rig::DeserializePreset(solo_rig::SerializePreset(preset));
    REQUIRE(restored.has_value());
    REQUIRE(restored->midi_bindings.has_value());
    REQUIRE(restored->midi_bindings->size() == 2U);
    REQUIRE(restored->midi_bindings->front().target_id == "drive-1.drive");
    REQUIRE(restored->midi_bindings->front().maximum == 16.0F);
    REQUIRE(restored->midi_bindings->back().target_id == "lead");

    const auto legacy = solo_rig::DeserializePreset(
        "{\"schemaVersion\":2,\"presetId\":\"old\",\"name\":\"Old\",\"nodes\":[],\"connections\":[]}");
    REQUIRE(legacy.has_value());
    REQUIRE(!legacy->midi_bindings.has_value());
}

TEST_CASE("RigPreset schema-v2 persists external module package references and rejects duplicates") {
    auto preset = CreatePreset();
    preset.preset_id = "package-rig";
    preset.external_module_packages = {
        {.module_id = "external.package", .root_directory = std::filesystem::path("packages/external")},
    };
    const auto restored = solo_rig::DeserializePreset(solo_rig::SerializePreset(preset));
    REQUIRE(restored.has_value());
    REQUIRE(restored->external_module_packages.size() == 1U);
    REQUIRE(restored->external_module_packages.front().module_id == "external.package");
    REQUIRE(restored->external_module_packages.front().root_directory == std::filesystem::path("packages/external"));

    preset.external_module_packages.push_back(preset.external_module_packages.front());
    const auto errors = solo_rig::ValidatePreset(preset);
    REQUIRE(std::ranges::any_of(errors, [](auto error) {
        return error == solo_rig::PresetValidationError::InvalidExternalModulePackage;
    }));
}

TEST_CASE("RigPreset schema-v2 persists direct NAM and IR asset references") {
    auto preset = CreatePreset();
    preset.preset_id = "direct-assets-rig";
    preset.external_module_packages = {
        {.module_id = "external.nam",
         .root_directory = std::filesystem::path("assets"),
         .source_kind = solo_rig::ExternalModuleSourceKind::NeuralAmpModel,
         .asset_path = std::filesystem::path("assets/model.nam")},
        {.module_id = "external.ir",
         .root_directory = std::filesystem::path("assets"),
         .source_kind = solo_rig::ExternalModuleSourceKind::ImpulseResponse,
         .asset_path = std::filesystem::path("assets/cabinet.wav")},
    };
    const auto restored = solo_rig::DeserializePreset(solo_rig::SerializePreset(preset));
    REQUIRE(restored.has_value());
    REQUIRE(restored->external_module_packages.size() == 2U);
    REQUIRE(restored->external_module_packages[0].source_kind ==
            solo_rig::ExternalModuleSourceKind::NeuralAmpModel);
    REQUIRE(restored->external_module_packages[0].asset_path == std::filesystem::path("assets/model.nam"));
    REQUIRE(restored->external_module_packages[1].source_kind ==
            solo_rig::ExternalModuleSourceKind::ImpulseResponse);
    REQUIRE(restored->external_module_packages[1].asset_path == std::filesystem::path("assets/cabinet.wav"));
}

TEST_CASE("RigPreset rejects malformed or out-of-range persisted MIDI bindings") {
    const auto invalid = solo_rig::DeserializePreset(
        "{\"schemaVersion\":2,\"presetId\":\"bad\",\"name\":\"Bad\",\"nodes\":[],\"connections\":[],"
        "\"midiBindings\":[{\"messageType\":\"controlChange\",\"channel\":0,\"number\":7,"
        "\"targetKind\":\"parameter\",\"targetId\":\"drive-1.drive\",\"minimum\":1.0,\"maximum\":0.0}]}");
    REQUIRE(!invalid.has_value());
}
