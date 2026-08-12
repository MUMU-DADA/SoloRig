#include "content/preset.hpp"
#include "content/json.hpp"
#include "test_framework.hpp"

#include <filesystem>
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
