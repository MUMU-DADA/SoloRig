#include "interop/solorig_backend.h"
#include "test_framework.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace {

void WriteAbiTestWave(const std::filesystem::path& path) {
    const std::array<std::uint8_t, 48> wave{
        'R', 'I', 'F', 'F', 40U, 0U, 0U, 0U, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 16U, 0U, 0U, 0U, 1U, 0U, 1U, 0U,
        0x80U, 0xBBU, 0U, 0U, 0U, 0x77U, 1U, 0U, 2U, 0U, 16U, 0U,
        'd', 'a', 't', 'a', 4U, 0U, 0U, 0U, 0xFFU, 0x7FU, 0U, 0U};
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(wave.data()), static_cast<std::streamsize>(wave.size()));
}

} // namespace

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
    REQUIRE(std::string(node.skin_id_utf8) == "solorig.default-pedal");
    REQUIRE(std::string(node.knob_skin_id_utf8) == "standard-knob");
    REQUIRE(std::string(node.footswitch_skin_id_utf8) == "standard-footswitch");
    REQUIRE(std::string(node.knob_image_path_utf8).empty());
    REQUIRE(node.bypassed == 0U);
    solorig_snapshot_destroy(snapshot);
    solorig_backend_destroy(backend);
}

TEST_CASE("C ABI backend toggles node bypass state through the editor transaction") {
    SoloRigBackend* backend = nullptr;
    REQUIRE(solorig_backend_create(&backend) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_set_node_bypassed_utf8(backend, "drive-1", 1U) == SOLO_RIG_STATUS_OK);

    SoloRigSnapshot* snapshot = nullptr;
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    SoloRigSnapshotInfo info{};
    REQUIRE(solorig_snapshot_info(snapshot, &info) == SOLO_RIG_STATUS_OK);
    REQUIRE(info.dirty != 0U);
    SoloRigNodeView node{};
    REQUIRE(solorig_snapshot_node(snapshot, 0U, &node) == SOLO_RIG_STATUS_OK);
    REQUIRE(node.bypassed != 0U);
    solorig_snapshot_destroy(snapshot);

    REQUIRE(solorig_backend_set_node_bypassed_utf8(backend, "drive-1", 2U) == SOLO_RIG_STATUS_INVALID_ARGUMENT);
    REQUIRE(solorig_backend_set_node_bypassed_utf8(backend, "missing", 1U) == SOLO_RIG_STATUS_NOT_FOUND);
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
            REQUIRE(parameter.minimum == 0.0F);
            REQUIRE(parameter.maximum == 16.0F);
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

TEST_CASE("C ABI exposes catalog, transactional editing and performance snapshot") {
    SoloRigBackend* backend = nullptr;
    REQUIRE(solorig_backend_create(&backend) == SOLO_RIG_STATUS_OK);
    uint32_t catalog_count = 0;
    REQUIRE(solorig_backend_catalog_count(backend, &catalog_count) == SOLO_RIG_STATUS_OK);
    REQUIRE(catalog_count >= 2U);
    SoloRigModuleCatalogView entry{};
    REQUIRE(solorig_backend_catalog_entry(backend, 0U, &entry) == SOLO_RIG_STATUS_OK);
    REQUIRE(entry.module_id_utf8 != nullptr);
    REQUIRE(entry.parameter_count > 0U);
    REQUIRE(solorig_backend_add_node_utf8(backend, "org.mumu-dada.solorig.drive", "drive-2", 640.0, 160.0) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_duplicate_node_utf8(backend, "drive-2", "drive-3") == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_connect_utf8(backend, "drive-1", "drive-2") == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_connect_utf8(backend, "drive-1", "drive-2") != SOLO_RIG_STATUS_OK);
    SoloRigSnapshot* snapshot = nullptr;
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    SoloRigSnapshotInfo info{};
    REQUIRE(solorig_snapshot_info(snapshot, &info) == SOLO_RIG_STATUS_OK);
    REQUIRE(info.node_count == 4U);
    REQUIRE(info.total_latency_samples > 0U);
    REQUIRE(info.estimated_cpu_percent > 0.0);
    REQUIRE(info.schema_version == 4U);
    REQUIRE(info.measured_node_count == 0U);
    REQUIRE(info.measurement_available == 0U);
    SoloRigNodeView node{};
    REQUIRE(solorig_snapshot_node(snapshot, 0U, &node) == SOLO_RIG_STATUS_OK);
    REQUIRE(node.estimated_cpu_percent > 0.0);
    REQUIRE(node.measurement_available == 0U);
    solorig_snapshot_destroy(snapshot);
    REQUIRE(solorig_backend_set_node_performance_utf8(
                backend, "drive-1", 7U, 3.5, 4.0) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_set_node_performance_utf8(
                backend, "missing", 0U, 1.0, 1.0) == SOLO_RIG_STATUS_NOT_FOUND);
    REQUIRE(solorig_backend_set_node_performance_utf8(
                backend, "drive-1", 0U, std::numeric_limits<double>::quiet_NaN(), 1.0) ==
            SOLO_RIG_STATUS_INVALID_ARGUMENT);
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_snapshot_info(snapshot, &info) == SOLO_RIG_STATUS_OK);
    REQUIRE(info.measured_node_count == 1U);
    REQUIRE(info.measurement_available == 0U);
    REQUIRE(solorig_snapshot_node(snapshot, 0U, &node) == SOLO_RIG_STATUS_OK);
    REQUIRE(node.declared_latency_samples == 7U);
    REQUIRE(node.measured_cpu_percent == 3.5);
    REQUIRE(node.peak_cpu_percent == 4.0);
    REQUIRE(node.measurement_available != 0U);
    solorig_snapshot_destroy(snapshot);
    REQUIRE(solorig_backend_set_node_performance_utf8(backend, "delay-1", 96U, 2.5, 3.0) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_set_node_performance_utf8(backend, "drive-2", 0U, 1.0, 1.5) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_set_node_performance_utf8(backend, "drive-3", 0U, 1.0, 1.5) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_snapshot_info(snapshot, &info) == SOLO_RIG_STATUS_OK);
    REQUIRE(info.measured_node_count == 4U);
    REQUIRE(info.measurement_available != 0U);
    REQUIRE(info.measured_cpu_percent == 8.0);
    REQUIRE(info.peak_cpu_percent == 10.0);
    solorig_snapshot_destroy(snapshot);
    REQUIRE(solorig_backend_clear_node_performance(backend) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_snapshot_info(snapshot, &info) == SOLO_RIG_STATUS_OK);
    REQUIRE(info.measured_node_count == 0U);
    REQUIRE(info.measurement_available == 0U);
    solorig_snapshot_destroy(snapshot);
    REQUIRE(solorig_backend_add_node_utf8(backend, "missing.module", "bad", 0.0, 0.0) == SOLO_RIG_STATUS_NOT_FOUND);
    REQUIRE(solorig_backend_delete_node_utf8(backend, "drive-3") == SOLO_RIG_STATUS_OK);
    solorig_backend_destroy(backend);
}

TEST_CASE("C ABI explicit resource import identifies supported assets without startup scanning") {
    SoloRigBackend* backend = nullptr;
    REQUIRE(solorig_backend_create(&backend) == SOLO_RIG_STATUS_OK);
    const auto path = std::filesystem::temp_directory_path() / "solorig-abi-resource.ir";
    WriteAbiTestWave(path);
    REQUIRE(solorig_backend_import_resource_utf8(backend, path.string().c_str()) == SOLO_RIG_STATUS_OK);
    SoloRigSnapshot* snapshot = nullptr;
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    SoloRigSnapshotInfo info{};
    REQUIRE(solorig_snapshot_info(snapshot, &info) == SOLO_RIG_STATUS_OK);
    REQUIRE(std::string(info.resource_status_utf8).find("IR") != std::string::npos);
    solorig_snapshot_destroy(snapshot);
    REQUIRE(solorig_backend_import_resource_utf8(backend, "missing.unknown") == SOLO_RIG_STATUS_INVALID_ARGUMENT);
    solorig_backend_destroy(backend);
    std::error_code cleanup;
    std::filesystem::remove(path, cleanup);
}

TEST_CASE("C ABI external module package import extends the catalog and persists its reference") {
    const auto directory = std::filesystem::temp_directory_path() / "solorig-abi-module-package";
    const auto preset_path = std::filesystem::temp_directory_path() / "solorig-abi-module-package.srig";
    std::error_code cleanup;
    std::filesystem::remove_all(directory, cleanup);
    std::filesystem::remove(preset_path, cleanup);
    std::filesystem::create_directories(directory / "logic");
    {
        std::ofstream manifest(directory / "manifest.json", std::ios::binary | std::ios::trunc);
        manifest << R"({
            "schemaVersion":1,
            "moduleId":"org.mumu-dada.solorig.abi-package",
            "displayName":"ABI Package",
            "category":"digital-algorithm",
            "logicFile":"logic/main.json",
            "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}],
            "parameters":[{"id":"gain","minimum":0,"maximum":2,"default":0.5}]
        })";
        std::ofstream logic(directory / "logic/main.json", std::ios::binary | std::ios::trunc);
        logic << R"({"schemaVersion":1,"kind":"digital-algorithm","algorithm":"linear-gain","operations":[{"type":"gain","parameter":"gain"}]})";
    }

    SoloRigBackend* backend = nullptr;
    REQUIRE(solorig_backend_create(&backend) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_import_resource_utf8(backend, directory.string().c_str()) == SOLO_RIG_STATUS_OK);
    uint32_t catalog_count = 0U;
    REQUIRE(solorig_backend_catalog_count(backend, &catalog_count) == SOLO_RIG_STATUS_OK);
    REQUIRE(catalog_count == 3U);
    bool found = false;
    for (uint32_t index = 0U; index < catalog_count; ++index) {
        SoloRigModuleCatalogView entry{};
        REQUIRE(solorig_backend_catalog_entry(backend, index, &entry) == SOLO_RIG_STATUS_OK);
        if (std::string(entry.module_id_utf8) == "org.mumu-dada.solorig.abi-package") {
            found = true;
            REQUIRE(entry.parameter_count == 1U);
        }
    }
    REQUIRE(found);
    REQUIRE(solorig_backend_add_node_utf8(backend, "org.mumu-dada.solorig.abi-package", "abi-package-1", 600.0, 160.0) ==
            SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_save_utf8(backend, preset_path.string().c_str()) == SOLO_RIG_STATUS_OK);
    solorig_backend_destroy(backend);

    REQUIRE(solorig_backend_create(&backend) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_open_utf8(backend, preset_path.string().c_str()) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_catalog_count(backend, &catalog_count) == SOLO_RIG_STATUS_OK);
    REQUIRE(catalog_count == 3U);
    SoloRigSnapshot* snapshot = nullptr;
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    SoloRigNodeView node{};
    REQUIRE(solorig_snapshot_node(snapshot, 2U, &node) == SOLO_RIG_STATUS_OK);
    REQUIRE(node.unresolved == 0U);
    solorig_snapshot_destroy(snapshot);
    solorig_backend_destroy(backend);
    std::filesystem::remove(preset_path, cleanup);
    std::filesystem::remove_all(directory, cleanup);
}

TEST_CASE("C ABI exposes explicit realtime and whitebox boundaries") {
    SoloRigBackend* backend = nullptr;
    REQUIRE(solorig_backend_create(&backend) == SOLO_RIG_STATUS_OK);

    SoloRigRealtimeContractView realtime{};
    REQUIRE(solorig_backend_realtime_contract(backend, &realtime) == SOLO_RIG_STATUS_OK);
    REQUIRE(realtime.contract_version == 1U);
    REQUIRE(realtime.process_noexcept != 0U);
    REQUIRE(realtime.process_allocates == 0U);
    REQUIRE(realtime.process_reads_files == 0U);
    REQUIRE(realtime.process_parses_json == 0U);
    REQUIRE(realtime.process_locks == 0U);
    REQUIRE(realtime.prepare_owns_compilation != 0U);
    REQUIRE(realtime.maximum_oversampling >= 1U && realtime.maximum_oversampling <= 16U);
    REQUIRE(realtime.thread_rule_utf8 != nullptr && std::string(realtime.thread_rule_utf8).find("audio") != std::string::npos);

    SoloRigWhiteboxStatusView whitebox{};
    REQUIRE(solorig_backend_whitebox_status(backend, &whitebox) == SOLO_RIG_STATUS_OK);
    REQUIRE(whitebox.schema_version == 1U);
    REQUIRE(whitebox.graph_status_utf8 != nullptr && *whitebox.graph_status_utf8 != '\0');
    REQUIRE(whitebox.runtime_status_utf8 != nullptr && *whitebox.runtime_status_utf8 != '\0');
    REQUIRE(whitebox.evidence_status_utf8 != nullptr && *whitebox.evidence_status_utf8 != '\0');
    REQUIRE(whitebox.diagnostic_utf8 != nullptr && std::string(whitebox.diagnostic_utf8).find("PedalKernel") != std::string::npos);

    REQUIRE(solorig_backend_realtime_contract(backend, nullptr) == SOLO_RIG_STATUS_INVALID_ARGUMENT);
    REQUIRE(std::string(solorig_backend_last_error_utf8(backend)).find("output") != std::string::npos);
    REQUIRE(solorig_backend_whitebox_status(backend, nullptr) == SOLO_RIG_STATUS_INVALID_ARGUMENT);
    solorig_backend_destroy(backend);
}

TEST_CASE("C ABI null destruction and status pointers are safe") {
    solorig_backend_destroy(nullptr);
    solorig_snapshot_destroy(nullptr);
    SoloRigRealtimeContractView realtime{};
    REQUIRE(solorig_backend_realtime_contract(nullptr, &realtime) == SOLO_RIG_STATUS_INVALID_ARGUMENT);
    REQUIRE(solorig_backend_last_error_utf8(nullptr) != nullptr);
}
