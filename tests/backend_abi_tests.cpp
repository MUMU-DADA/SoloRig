#include "interop/solorig_backend.h"
#include "test_framework.hpp"

#include <cmath>
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

TEST_CASE("C ABI backend exposes DPI-stable selection, drag snapping and connections") {
    SoloRigBackend* backend = nullptr;
    REQUIRE(solorig_backend_create(&backend) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_set_viewport(backend, 1600.0, 900.0, 2.0) == SOLO_RIG_STATUS_OK);

    SoloRigSnapshot* snapshot = nullptr;
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    SoloRigSnapshotInfo info{};
    REQUIRE(solorig_snapshot_info(snapshot, &info) == SOLO_RIG_STATUS_OK);
    REQUIRE(info.viewport_width == 1600.0);
    REQUIRE(info.dpi_scale == 2.0);
    REQUIRE(info.connection_count == 1U);
    SoloRigConnectionView connection{};
    REQUIRE(solorig_snapshot_connection(snapshot, 0U, &connection) == SOLO_RIG_STATUS_OK);
    REQUIRE(std::string(connection.source_instance_id_utf8) == "drive-1");
    solorig_snapshot_destroy(snapshot);

    REQUIRE(solorig_backend_select_at(backend, 148.0, 316.0) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_begin_drag(backend, 148.0, 316.0) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_drag_to(backend, 700.0, 500.0, 1U) == SOLO_RIG_STATUS_OK);
    solorig_backend_end_drag(backend);
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    SoloRigSnapshotInfo after_drag{};
    REQUIRE(solorig_snapshot_info(snapshot, &after_drag) == SOLO_RIG_STATUS_OK);
    SoloRigNodeView node{};
    REQUIRE(solorig_snapshot_node(snapshot, 0U, &node) == SOLO_RIG_STATUS_OK);
    REQUIRE(node.layout_x >= 0.0F);
    REQUIRE(std::fmod(node.layout_x, 16.0F) == 0.0F);
    REQUIRE(node.selected != 0U);
    REQUIRE(after_drag.dirty != 0U);
    solorig_snapshot_destroy(snapshot);
    solorig_backend_destroy(backend);
}

TEST_CASE("C ABI backend rejects invalid editor commands without changing the snapshot") {
    SoloRigBackend* backend = nullptr;
    REQUIRE(solorig_backend_create(&backend) == SOLO_RIG_STATUS_OK);
    SoloRigSnapshot* snapshot = nullptr;
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    SoloRigSnapshotInfo before{};
    REQUIRE(solorig_snapshot_info(snapshot, &before) == SOLO_RIG_STATUS_OK);
    solorig_snapshot_destroy(snapshot);
    REQUIRE(solorig_backend_set_viewport(backend, 1.0, 2.0, 0.0) == SOLO_RIG_STATUS_INVALID_ARGUMENT);
    REQUIRE(solorig_backend_select_at(backend, -1.0, -1.0) == SOLO_RIG_STATUS_NOT_FOUND);
    REQUIRE(solorig_backend_begin_drag(backend, -1.0, -1.0) == SOLO_RIG_STATUS_NOT_FOUND);
    REQUIRE(solorig_backend_drag_to(backend, 12.0, 14.0, 2U) == SOLO_RIG_STATUS_INVALID_ARGUMENT);
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    SoloRigSnapshotInfo after{};
    REQUIRE(solorig_snapshot_info(snapshot, &after) == SOLO_RIG_STATUS_OK);
    REQUIRE(after.viewport_width == before.viewport_width);
    REQUIRE(after.viewport_height == before.viewport_height);
    REQUIRE(after.dirty == before.dirty);
    solorig_snapshot_destroy(snapshot);
    solorig_backend_destroy(backend);
}
