#include "interop/solorig_backend.h"
#include "test_framework.hpp"

#include <filesystem>
#include <string>

TEST_CASE("C ABI backend creates a side-effect-free default snapshot") {
    SoloRigBackend* backend = nullptr;
    REQUIRE(solorig_backend_create(&backend) == SOLO_RIG_STATUS_OK);
    REQUIRE(backend != nullptr);

    SoloRigSnapshot* snapshot = nullptr;
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    REQUIRE(snapshot != nullptr);
    SoloRigSnapshotInfo info{};
    REQUIRE(solorig_snapshot_info(snapshot, &info) == SOLO_RIG_STATUS_OK);
    REQUIRE(info.node_count >= 2U);
    REQUIRE(info.source_path_utf8 != nullptr);
    REQUIRE(info.source_path_utf8[0] == '\0');
    REQUIRE(std::string(info.midi_status_utf8).find("closed") != std::string::npos);
    REQUIRE(info.dark_theme != 0U);
    SoloRigNodeView node{};
    REQUIRE(solorig_snapshot_node(snapshot, 0U, &node) == SOLO_RIG_STATUS_OK);
    REQUIRE(std::string(node.instance_id_utf8) == "drive-1");
    REQUIRE(node.parameter_count >= 1U);
    solorig_snapshot_destroy(snapshot);
    solorig_backend_destroy(backend);
}

TEST_CASE("C ABI backend performs a real parameter edit and preserves ownership rules") {
    SoloRigBackend* backend = nullptr;
    REQUIRE(solorig_backend_create(&backend) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_set_parameter_utf8(backend, "drive-1", "drive", 6.0F) == SOLO_RIG_STATUS_OK);

    SoloRigSnapshot* snapshot = nullptr;
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    SoloRigParameterView parameter{};
    bool drive_found = false;
    SoloRigNodeView node{};
    REQUIRE(solorig_snapshot_node(snapshot, 0U, &node) == SOLO_RIG_STATUS_OK);
    for (uint32_t index = 0; index < node.parameter_count; ++index) {
        REQUIRE(solorig_snapshot_parameter(snapshot, 0U, index, &parameter) == SOLO_RIG_STATUS_OK);
        if (std::string(parameter.parameter_id_utf8) == "drive") {
            drive_found = true;
            REQUIRE(parameter.value == 6.0F);
        }
    }
    REQUIRE(drive_found);
    REQUIRE(solorig_backend_set_theme_dark(backend, 0U) == SOLO_RIG_STATUS_OK);
    solorig_snapshot_destroy(snapshot);
    snapshot = nullptr;
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    SoloRigSnapshotInfo info{};
    REQUIRE(solorig_snapshot_info(snapshot, &info) == SOLO_RIG_STATUS_OK);
    REQUIRE(info.dark_theme == 0U);
    REQUIRE(solorig_backend_set_parameter_utf8(backend, "missing", "parameter", 1.0F) == SOLO_RIG_STATUS_NOT_FOUND);
    REQUIRE(std::string(solorig_backend_last_error_utf8(backend)).find("not found") != std::string::npos);
    solorig_snapshot_destroy(snapshot);
    solorig_backend_destroy(backend);
}

TEST_CASE("C ABI backend exposes explicit user file operations and protects invalid paths") {
    SoloRigBackend* backend = nullptr;
    REQUIRE(solorig_backend_create(&backend) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_save_utf8(backend, "not-a-preset.txt") == SOLO_RIG_STATUS_INVALID_ARGUMENT);
    const auto path = std::filesystem::temp_directory_path() / "solorig-c-abi.srig";
    std::error_code cleanup;
    std::filesystem::remove(path, cleanup);
    const auto path_utf8 = path.string();
    REQUIRE(solorig_backend_save_utf8(backend, path_utf8.c_str()) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_open_utf8(backend, path_utf8.c_str()) == SOLO_RIG_STATUS_OK);
    std::filesystem::remove(path, cleanup);
    solorig_backend_destroy(backend);
}
