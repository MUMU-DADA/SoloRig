#include "interop/solorig_backend.h"
#include "test_framework.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>

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
    uint32_t port_count = 0U;
    REQUIRE(solorig_snapshot_node_port_count(snapshot, 0U, &port_count) == SOLO_RIG_STATUS_OK);
    REQUIRE(port_count == 2U);
    SoloRigNodePortView input_port{};
    SoloRigNodePortView output_port{};
    REQUIRE(solorig_snapshot_node_port(snapshot, 0U, 0U, &input_port) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_snapshot_node_port(snapshot, 0U, 1U, &output_port) == SOLO_RIG_STATUS_OK);
    REQUIRE(std::string(input_port.port_id_utf8) == "input");
    REQUIRE(input_port.direction == 0U);
    REQUIRE(input_port.max_channels == 2U);
    REQUIRE(std::string(output_port.port_id_utf8) == "output");
    REQUIRE(output_port.direction == 1U);
    REQUIRE(output_port.max_channels == 2U);
    SoloRigNodeResourceView resource{};
    REQUIRE(solorig_snapshot_node_resource(snapshot, 0U, &resource) == SOLO_RIG_STATUS_OK);
    REQUIRE(std::string(resource.kind_utf8) == "Built-in module");
    REQUIRE(std::string(resource.path_utf8) == "Embedded runtime");
    REQUIRE(std::string(resource.status_utf8) == "Ready");
    REQUIRE(solorig_snapshot_node_port_count(snapshot, info.node_count, &port_count) ==
            SOLO_RIG_STATUS_INVALID_ARGUMENT);
    REQUIRE(solorig_snapshot_node_resource(snapshot, info.node_count, &resource) ==
            SOLO_RIG_STATUS_INVALID_ARGUMENT);
    solorig_snapshot_destroy(snapshot);
    solorig_backend_destroy(backend);
}

TEST_CASE("C ABI routed ASIO entry point validates route arrays without opening hardware") {
    SoloRigBackend* backend = nullptr;
    REQUIRE(solorig_backend_create(&backend) == SOLO_RIG_STATUS_OK);
    SoloRigAsioChannelRoute route{.driver_index = 0U, .device_channel = 0U, .processing_channel = 0U};
    REQUIRE(solorig_backend_asio_open_routed(backend, nullptr, nullptr, 1U, &route, 1U) ==
            SOLO_RIG_STATUS_INVALID_ARGUMENT);
    REQUIRE(solorig_backend_asio_open_routed(backend, nullptr, &route, 1U, &route, 1U) ==
            SOLO_RIG_STATUS_NOT_FOUND);
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

TEST_CASE("C ABI backend batches node bypass changes into one reversible transaction") {
    SoloRigBackend* backend = nullptr;
    REQUIRE(solorig_backend_create(&backend) == SOLO_RIG_STATUS_OK);
    const std::array<SoloRigNodeBatchEntry, 2> nodes{{
        {.instance_id_utf8 = "drive-1"},
        {.instance_id_utf8 = "delay-1"},
    }};
    REQUIRE(solorig_backend_set_nodes_bypassed(backend, nodes.data(),
                                               static_cast<uint32_t>(nodes.size()), 1U) ==
            SOLO_RIG_STATUS_OK);

    SoloRigSnapshot* snapshot = nullptr;
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    SoloRigSnapshotInfo info{};
    REQUIRE(solorig_snapshot_info(snapshot, &info) == SOLO_RIG_STATUS_OK);
    REQUIRE(info.node_count == 2U);
    REQUIRE(info.dirty != 0U);
    for (uint32_t index = 0U; index < info.node_count; ++index) {
        SoloRigNodeView node{};
        REQUIRE(solorig_snapshot_node(snapshot, index, &node) == SOLO_RIG_STATUS_OK);
        REQUIRE(node.bypassed != 0U);
    }
    solorig_snapshot_destroy(snapshot);

    REQUIRE(solorig_backend_can_undo(backend) != 0U);
    REQUIRE(solorig_backend_undo(backend) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_snapshot_info(snapshot, &info) == SOLO_RIG_STATUS_OK);
    REQUIRE(info.dirty == 0U);
    for (uint32_t index = 0U; index < info.node_count; ++index) {
        SoloRigNodeView node{};
        REQUIRE(solorig_snapshot_node(snapshot, index, &node) == SOLO_RIG_STATUS_OK);
        REQUIRE(node.bypassed == 0U);
    }
    solorig_snapshot_destroy(snapshot);

    const std::array<SoloRigNodeBatchEntry, 2> duplicate_nodes{{
        {.instance_id_utf8 = "drive-1"},
        {.instance_id_utf8 = "drive-1"},
    }};
    REQUIRE(solorig_backend_set_nodes_bypassed(backend, duplicate_nodes.data(),
                                               static_cast<uint32_t>(duplicate_nodes.size()), 1U) ==
            SOLO_RIG_STATUS_INVALID_ARGUMENT);
    const std::array<SoloRigNodeBatchEntry, 2> missing_nodes{{
        {.instance_id_utf8 = "drive-1"},
        {.instance_id_utf8 = "missing"},
    }};
    REQUIRE(solorig_backend_set_nodes_bypassed(backend, missing_nodes.data(),
                                               static_cast<uint32_t>(missing_nodes.size()), 1U) ==
            SOLO_RIG_STATUS_NOT_FOUND);
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_snapshot_info(snapshot, &info) == SOLO_RIG_STATUS_OK);
    for (uint32_t index = 0U; index < info.node_count; ++index) {
        SoloRigNodeView node{};
        REQUIRE(solorig_snapshot_node(snapshot, index, &node) == SOLO_RIG_STATUS_OK);
        REQUIRE(node.bypassed == 0U);
    }
    solorig_snapshot_destroy(snapshot);
    solorig_backend_destroy(backend);
}

TEST_CASE("C ABI backend performs a real parameter edit and preserves ownership rules") {
    SoloRigBackend* backend = nullptr;
    REQUIRE(solorig_backend_create(&backend) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_set_parameter_utf8(backend, "drive-1", "drive", 6.0F) == SOLO_RIG_STATUS_OK);

    SoloRigSnapshot* snapshot = nullptr;
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    SoloRigSnapshotInfo info{};
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
    REQUIRE(solorig_backend_can_undo(backend) != 0U);
    REQUIRE(solorig_backend_can_redo(backend) == 0U);
    REQUIRE(solorig_backend_undo(backend) == SOLO_RIG_STATUS_OK);
    solorig_snapshot_destroy(snapshot);
    snapshot = nullptr;
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_snapshot_info(snapshot, &info) == SOLO_RIG_STATUS_OK);
    REQUIRE(info.dirty == 0U);
    REQUIRE(solorig_snapshot_node(snapshot, 0U, &node) == SOLO_RIG_STATUS_OK);
    bool undo_restored_drive = false;
    for (uint32_t index = 0; index < node.parameter_count; ++index) {
        REQUIRE(solorig_snapshot_parameter(snapshot, 0U, index, &parameter) == SOLO_RIG_STATUS_OK);
        if (std::string(parameter.parameter_id_utf8) == "drive") {
            undo_restored_drive = parameter.value == 4.0F;
        }
    }
    REQUIRE(undo_restored_drive);
    REQUIRE(solorig_backend_can_redo(backend) != 0U);
    REQUIRE(solorig_backend_redo(backend) == SOLO_RIG_STATUS_OK);
    solorig_snapshot_destroy(snapshot);
    snapshot = nullptr;
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_snapshot_info(snapshot, &info) == SOLO_RIG_STATUS_OK);
    REQUIRE(info.dirty != 0U);
    REQUIRE(solorig_snapshot_node(snapshot, 0U, &node) == SOLO_RIG_STATUS_OK);
    bool redo_restored_drive = false;
    for (uint32_t index = 0; index < node.parameter_count; ++index) {
        REQUIRE(solorig_snapshot_parameter(snapshot, 0U, index, &parameter) == SOLO_RIG_STATUS_OK);
        if (std::string(parameter.parameter_id_utf8) == "drive") {
            redo_restored_drive = parameter.value == 6.0F;
        }
    }
    REQUIRE(redo_restored_drive);
    REQUIRE(solorig_backend_undo(backend) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_clear_history(backend) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_can_undo(backend) == 0U);
    REQUIRE(solorig_backend_can_redo(backend) == 0U);
    REQUIRE(solorig_backend_set_theme_dark(backend, 0U) == SOLO_RIG_STATUS_OK);
    solorig_snapshot_destroy(snapshot);
    snapshot = nullptr;
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
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
    REQUIRE(solorig_backend_can_undo(backend) != 0U);
    REQUIRE(solorig_backend_undo(backend) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    SoloRigSnapshotInfo after_undo{};
    REQUIRE(solorig_snapshot_info(snapshot, &after_undo) == SOLO_RIG_STATUS_OK);
    REQUIRE(after_undo.dirty == 0U);
    REQUIRE(solorig_snapshot_node(snapshot, 0U, &node) == SOLO_RIG_STATUS_OK);
    REQUIRE(node.layout_x == 128.0F);
    REQUIRE(node.layout_y == 296.0F);
    solorig_snapshot_destroy(snapshot);
    REQUIRE(solorig_backend_can_redo(backend) != 0U);
    REQUIRE(solorig_backend_redo(backend) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_clear_selection(backend) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_snapshot_node(snapshot, 0U, &node) == SOLO_RIG_STATUS_OK);
    REQUIRE(node.selected == 0U);
    solorig_snapshot_destroy(snapshot);
    solorig_backend_destroy(backend);
}

TEST_CASE("C ABI adds a node atomically to the visible chain and restores it") {
    SoloRigBackend* backend = nullptr;
    REQUIRE(solorig_backend_create(&backend) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_add_node_to_chain_utf8(
                backend, "org.mumu-dada.solorig.drive", "drive-2", 640.0, 160.0) == SOLO_RIG_STATUS_OK);

    SoloRigSnapshot* snapshot = nullptr;
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    SoloRigSnapshotInfo info{};
    REQUIRE(solorig_snapshot_info(snapshot, &info) == SOLO_RIG_STATUS_OK);
    REQUIRE(info.node_count == 3U);
    REQUIRE(info.connection_count == 2U);
    SoloRigConnectionView connection{};
    REQUIRE(solorig_snapshot_connection(snapshot, 1U, &connection) == SOLO_RIG_STATUS_OK);
    REQUIRE(std::string(connection.destination_instance_id_utf8) == "drive-2");
    solorig_snapshot_destroy(snapshot);

    REQUIRE(solorig_backend_undo(backend) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_snapshot_create(backend, &snapshot) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_snapshot_info(snapshot, &info) == SOLO_RIG_STATUS_OK);
    REQUIRE(info.node_count == 2U);
    REQUIRE(info.connection_count == 1U);
    REQUIRE(info.dirty == 0U);
    solorig_snapshot_destroy(snapshot);

    REQUIRE(solorig_backend_redo(backend) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_clear_history(backend) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_can_undo(backend) == 0U);
    REQUIRE(solorig_backend_can_redo(backend) == 0U);
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
    SoloRigAudioStateView audio_state{};
    REQUIRE(solorig_snapshot_audio_state(snapshot, &audio_state) == SOLO_RIG_STATUS_OK);
    REQUIRE(audio_state.is_open == 0U);
    REQUIRE(audio_state.is_running == 0U);
    REQUIRE(audio_state.reset_requested == 0U);
    REQUIRE(audio_state.dropout_reported == 0U);
    REQUIRE(audio_state.sample_rate_changed_to == 0.0);
    SoloRigTunerStateView tuner_state{};
    REQUIRE(solorig_snapshot_tuner_state(snapshot, &tuner_state) == SOLO_RIG_STATUS_OK);
    REQUIRE(tuner_state.enabled == 0U);
    REQUIRE(tuner_state.signal_present == 0U);
    REQUIRE(tuner_state.midi_note == -1);
    REQUIRE(tuner_state.frequency_hz == 0.0);
    REQUIRE(tuner_state.cents_from_nearest_note == 0.0);
    REQUIRE(tuner_state.confidence == 0.0);
    REQUIRE(solorig_snapshot_tuner_state(nullptr, &tuner_state) == SOLO_RIG_STATUS_INVALID_ARGUMENT);
    REQUIRE(solorig_snapshot_tuner_state(snapshot, nullptr) == SOLO_RIG_STATUS_INVALID_ARGUMENT);
    SoloRigMidiStateView midi_state{};
    REQUIRE(solorig_snapshot_midi_state(snapshot, &midi_state) == SOLO_RIG_STATUS_OK);
    REQUIRE(midi_state.is_open == 0U);
    REQUIRE(midi_state.learn_armed == 0U);
    REQUIRE(midi_state.state == 0U);
    REQUIRE(midi_state.selected_device_index == 0U);
    REQUIRE(midi_state.dropped_message_count == 0U);
    REQUIRE(solorig_snapshot_midi_state(nullptr, &midi_state) == SOLO_RIG_STATUS_INVALID_ARGUMENT);
    REQUIRE(solorig_snapshot_midi_state(snapshot, nullptr) == SOLO_RIG_STATUS_INVALID_ARGUMENT);
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
    uint32_t catalog_count = 0U;
    REQUIRE(solorig_backend_catalog_count(backend, &catalog_count) == SOLO_RIG_STATUS_OK);
    REQUIRE(catalog_count == 2U);
    REQUIRE(solorig_backend_validate_module_package_utf8(backend, directory.string().c_str()) == SOLO_RIG_STATUS_OK);
    SoloRigModuleAuditionView audition{};
    REQUIRE(solorig_backend_audition_module_package_utf8(backend, directory.string().c_str(), &audition) ==
            SOLO_RIG_STATUS_OK);
    REQUIRE(audition.sample_rate_hz == 48000U);
    REQUIRE(audition.block_size == 256U);
    REQUIRE(audition.channel_count == 1U);
    REQUIRE(audition.output_finite != 0U);
    REQUIRE(std::isfinite(audition.output_peak));
    REQUIRE(std::isfinite(audition.output_rms));
    REQUIRE(std::isfinite(audition.processing_cpu_percent));
    REQUIRE(solorig_backend_catalog_count(backend, &catalog_count) == SOLO_RIG_STATUS_OK);
    REQUIRE(catalog_count == 2U);
    REQUIRE(solorig_backend_validate_module_package_utf8(backend, "missing-module-package") == SOLO_RIG_STATUS_NOT_FOUND);
    REQUIRE(solorig_backend_audition_module_package_utf8(backend, directory.string().c_str(), nullptr) ==
            SOLO_RIG_STATUS_INVALID_ARGUMENT);
    REQUIRE(solorig_backend_import_resource_utf8(backend, directory.string().c_str()) == SOLO_RIG_STATUS_OK);
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

TEST_CASE("C ABI module validation compiles multi-port circuits and reports runtime diagnostics") {
    const auto root = std::filesystem::temp_directory_path() / "solorig-abi-runtime-validation";
    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
    const auto write_package = [&root](std::string_view directory_name, std::string_view module_id,
                                      std::string_view ports, std::string_view circuit) {
        const auto directory = root / directory_name;
        std::filesystem::create_directories(directory / "logic");
        std::ofstream(directory / "manifest.json", std::ios::binary | std::ios::trunc)
            << "{\"schemaVersion\":1,\"moduleId\":\"" << module_id
            << "\",\"displayName\":\"ABI Runtime Validation\",\"category\":\"analog-circuit\","
               "\"logicFile\":\"logic/main.json\",\"ports\":" << ports << "}";
        std::ofstream(directory / "logic" / "main.json", std::ios::binary | std::ios::trunc) << circuit;
        return directory;
    };
    const auto multi_port = write_package("multi-port", "org.mumu-dada.solorig.abi-multi-port",
        R"([{"id":"input-a","direction":"input","maxChannels":1},
             {"id":"input-b","direction":"input","maxChannels":1},
             {"id":"output","direction":"output","maxChannels":1}])",
        R"({"version":2,"schema":"circuit","referenceGround":"0","nodes":["a","b","out","0"],
             "ports":[{"id":"input-a","direction":"input","node":"a"},
                      {"id":"input-b","direction":"input","node":"b"},
                      {"id":"output","direction":"output","node":"out"}],
             "components":[{"id":"ra","type":"resistor","pins":["a","out"],"value":1000},
                           {"id":"rb","type":"resistor","pins":["b","out"],"value":3000},
                           {"id":"load","type":"resistor","pins":["out","0"],"value":1000}],
             "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}})" );
    const auto singular = write_package("singular", "org.mumu-dada.solorig.abi-singular",
        R"([{"id":"input","direction":"input","maxChannels":1},
             {"id":"output","direction":"output","maxChannels":1}])",
        R"({"version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","floating","0"],
             "ports":[{"id":"input","direction":"input","node":"in"},
                      {"id":"output","direction":"output","node":"out"}],
             "components":[{"id":"series","type":"resistor","pins":["in","out"],"value":1000},
                           {"id":"load","type":"resistor","pins":["out","0"],"value":1000}],
             "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}})" );
    const auto unauthorized = write_package("unauthorized", "org.mumu-dada.solorig.abi-unauthorized",
        R"([{"id":"input","direction":"input","maxChannels":1},
             {"id":"output","direction":"output","maxChannels":1}])",
        R"({"version":2,"schema":"circuit","referenceGround":"0","nodes":["vcc","c","b","0"],
             "ports":[{"id":"input","direction":"input","node":"b"},
                      {"id":"output","direction":"output","node":"c"}],
             "supplies":[{"id":"vcc","positive":"vcc","negative":"0","voltage":5}],
             "components":[{"id":"load","type":"resistor","pins":["vcc","c"],"value":1000},
                           {"id":"q","type":"bjt","pins":["c","b","0"],
                            "model":"solorig.cleanroom.bjt.npn.v1"}],
             "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}})" );
    const auto nonconvergent = write_package("nonconvergent", "org.mumu-dada.solorig.abi-nonconvergent",
        R"([{"id":"input","direction":"input","maxChannels":1},
             {"id":"output","direction":"output","maxChannels":1}])",
        R"({"version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","vcc","0"],
             "ports":[{"id":"input","direction":"input","node":"in"},
                      {"id":"output","direction":"output","node":"out"}],
             "supplies":[{"id":"vcc","positive":"vcc","negative":"0","voltage":5}],
             "components":[{"id":"input-return","type":"resistor","pins":["in","0"],"value":1000},
                           {"id":"feed","type":"resistor","pins":["vcc","out"],"value":1000},
                           {"id":"diode","type":"diode","pins":["out","0"],
                            "model":{"kind":"shockley","modelVersion":1,"saturationCurrent":1e-12,
                                     "thermalVoltage":0.02585,"maxStep":0.5}}],
             "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1,
                       "convergence":{"maxIterations":1,"absoluteTolerance":1e-12,
                                      "relativeTolerance":1e-12}}})" );
    const auto potentiometer = root / "potentiometer";
    std::filesystem::create_directories(potentiometer / "logic");
    std::ofstream(potentiometer / "manifest.json", std::ios::binary | std::ios::trunc)
        << R"({"schemaVersion":1,"moduleId":"org.mumu-dada.solorig.abi-potentiometer",
             "displayName":"ABI Potentiometer","category":"analog-circuit",
             "logicFile":"logic/main.json",
             "ports":[{"id":"input","direction":"input","maxChannels":1},
                      {"id":"output","direction":"output","maxChannels":1}],
             "parameters":[{"id":"position","minimum":0,"maximum":1,"default":0.5}]})";
    std::ofstream(potentiometer / "logic" / "main.json", std::ios::binary | std::ios::trunc)
        << R"({"version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
             "ports":[{"id":"input","direction":"input","node":"in"},
                      {"id":"output","direction":"output","node":"out"}],
             "components":[{"id":"pot","type":"potentiometer","pins":["in","0","out"],
                            "value":10000,
                            "potentiometer":{"position":0.5,"minimum":0,"maximum":1}}],
             "parameterBindings":[{"modelVersion":1,"parameter":"position","target":"pot",
                                   "property":"potentiometer.position","smoothingSeconds":0}],
             "solver":{"method":"modified-nodal-analysis","integration":"backward-euler",
                       "oversampling":1}})";

    SoloRigBackend* backend = nullptr;
    REQUIRE(solorig_backend_create(&backend) == SOLO_RIG_STATUS_OK);
    REQUIRE(solorig_backend_validate_module_package_utf8(backend, multi_port.string().c_str()) ==
            SOLO_RIG_STATUS_OK);

    std::array<float, 256> input_waveform{};
    std::array<float, 256> output_waveform{};
    SoloRigModuleAuditionConfigV2 audition_config{
        .sample_rate_hz = 48000U,
        .block_size = static_cast<std::uint32_t>(input_waveform.size()),
        .channel_count = 1U,
        .stimulus_kind = 0U,
        .input_port_utf8 = "input-a",
        .output_port_utf8 = "output",
        .amplitude = 0.2,
        .start_frequency_hz = 220.0,
        .end_frequency_hz = 4000.0,
    };
    SoloRigModuleAuditionAnalysisV2 input_a_analysis{};
    REQUIRE(solorig_backend_audition_module_package_v2_utf8(
                backend, multi_port.string().c_str(), &audition_config,
                input_waveform.data(), output_waveform.data(),
                static_cast<std::uint32_t>(input_waveform.size()), &input_a_analysis) ==
            SOLO_RIG_STATUS_OK);
    REQUIRE(input_a_analysis.schema_version == 2U);
    REQUIRE(input_a_analysis.waveform_sample_count == input_waveform.size());
    REQUIRE(input_a_analysis.output_finite != 0U);
    REQUIRE(input_a_analysis.input_peak > 0.19);
    REQUIRE(input_a_analysis.output_rms > 0.0);
    SoloRigModuleAuditionAnalysisV3 diagnostics_analysis{};
    REQUIRE(solorig_backend_audition_module_package_v3_utf8(
                backend, multi_port.string().c_str(), &audition_config,
                input_waveform.data(), output_waveform.data(),
                static_cast<std::uint32_t>(input_waveform.size()), &diagnostics_analysis) ==
            SOLO_RIG_STATUS_OK);
    REQUIRE(diagnostics_analysis.schema_version == 3U);
    REQUIRE(diagnostics_analysis.solver_processed_sample_count == input_waveform.size());
    REQUIRE(diagnostics_analysis.solver_total_iterations >= diagnostics_analysis.solver_processed_sample_count);
    REQUIRE(diagnostics_analysis.solver_maximum_iterations >= 1U);
    REQUIRE(diagnostics_analysis.solver_failure_count == 0U);

    audition_config.input_port_utf8 = "input";
    audition_config.output_port_utf8 = "output";
    audition_config.stimulus_kind = 0U;
    SoloRigModuleAuditionAnalysisV3 low_position_analysis{};
    REQUIRE(solorig_backend_audition_module_package_v4_utf8(
                backend, potentiometer.string().c_str(), &audition_config, "position", 0.25,
                input_waveform.data(), output_waveform.data(),
                static_cast<std::uint32_t>(input_waveform.size()), &low_position_analysis) ==
            SOLO_RIG_STATUS_OK);
    SoloRigModuleAuditionAnalysisV3 high_position_analysis{};
    REQUIRE(solorig_backend_audition_module_package_v4_utf8(
                backend, potentiometer.string().c_str(), &audition_config, "position", 0.75,
                input_waveform.data(), output_waveform.data(),
                static_cast<std::uint32_t>(input_waveform.size()), &high_position_analysis) ==
            SOLO_RIG_STATUS_OK);
    REQUIRE(low_position_analysis.output_rms > high_position_analysis.output_rms * 2.5);
    REQUIRE(low_position_analysis.solver_failure_count == 0U);
    REQUIRE(high_position_analysis.solver_failure_count == 0U);
    REQUIRE(solorig_backend_audition_module_package_v4_utf8(
                backend, potentiometer.string().c_str(), &audition_config, "missing", 0.5,
                input_waveform.data(), output_waveform.data(),
                static_cast<std::uint32_t>(input_waveform.size()), &high_position_analysis) ==
            SOLO_RIG_STATUS_INVALID_ARGUMENT);
    REQUIRE(solorig_backend_audition_module_package_v4_utf8(
                backend, potentiometer.string().c_str(), &audition_config, "position", 1.1,
                input_waveform.data(), output_waveform.data(),
                static_cast<std::uint32_t>(input_waveform.size()), &high_position_analysis) ==
            SOLO_RIG_STATUS_INVALID_ARGUMENT);
    REQUIRE(solorig_backend_audition_module_package_v4_utf8(
                backend, potentiometer.string().c_str(), &audition_config, "position",
                std::numeric_limits<double>::quiet_NaN(), input_waveform.data(), output_waveform.data(),
                static_cast<std::uint32_t>(input_waveform.size()), &high_position_analysis) ==
            SOLO_RIG_STATUS_INVALID_ARGUMENT);

    audition_config.input_port_utf8 = "input-b";
    SoloRigModuleAuditionAnalysisV2 input_b_analysis{};
    REQUIRE(solorig_backend_audition_module_package_v2_utf8(
                backend, multi_port.string().c_str(), &audition_config,
                input_waveform.data(), output_waveform.data(),
                static_cast<std::uint32_t>(input_waveform.size()), &input_b_analysis) ==
            SOLO_RIG_STATUS_OK);
    REQUIRE(input_a_analysis.output_rms > input_b_analysis.output_rms * 2.0);

    audition_config.stimulus_kind = 1U;
    SoloRigModuleAuditionAnalysisV2 impulse_analysis{};
    REQUIRE(solorig_backend_audition_module_package_v2_utf8(
                backend, multi_port.string().c_str(), &audition_config,
                input_waveform.data(), output_waveform.data(),
                static_cast<std::uint32_t>(input_waveform.size()), &impulse_analysis) ==
            SOLO_RIG_STATUS_OK);
    REQUIRE(std::abs(input_waveform[0] - 0.2F) < 0.000001F);
    REQUIRE(input_waveform[1] == 0.0F);
    REQUIRE(impulse_analysis.output_finite != 0U);

    audition_config.stimulus_kind = 2U;
    SoloRigModuleAuditionAnalysisV2 sweep_analysis{};
    REQUIRE(solorig_backend_audition_module_package_v2_utf8(
                backend, multi_port.string().c_str(), &audition_config,
                input_waveform.data(), output_waveform.data(),
                static_cast<std::uint32_t>(input_waveform.size()), &sweep_analysis) ==
            SOLO_RIG_STATUS_OK);
    REQUIRE(input_waveform[0] == 0.0F);
    REQUIRE(sweep_analysis.input_rms > 0.0);
    REQUIRE(sweep_analysis.output_finite != 0U);

    audition_config.stimulus_kind = 3U;
    for (std::size_t index = 0U; index < input_waveform.size(); ++index) {
        input_waveform[index] = static_cast<float>(index) / static_cast<float>(input_waveform.size()) * 0.2F - 0.1F;
    }
    const auto caller_first = input_waveform.front();
    const auto caller_last = input_waveform.back();
    SoloRigModuleAuditionAnalysisV2 caller_waveform_analysis{};
    REQUIRE(solorig_backend_audition_module_package_v2_utf8(
                backend, multi_port.string().c_str(), &audition_config,
                input_waveform.data(), output_waveform.data(),
                static_cast<std::uint32_t>(input_waveform.size()), &caller_waveform_analysis) ==
            SOLO_RIG_STATUS_OK);
    REQUIRE(std::abs(input_waveform.front() - caller_first) < 0.000001F);
    REQUIRE(std::abs(input_waveform.back() - caller_last) < 0.000001F);
    REQUIRE(caller_waveform_analysis.input_peak > 0.09);
    input_waveform[17] = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(solorig_backend_audition_module_package_v2_utf8(
                backend, multi_port.string().c_str(), &audition_config,
                input_waveform.data(), output_waveform.data(),
                static_cast<std::uint32_t>(input_waveform.size()), &caller_waveform_analysis) ==
            SOLO_RIG_STATUS_INVALID_ARGUMENT);
    input_waveform[17] = 0.0F;

    audition_config.input_port_utf8 = "missing";
    REQUIRE(solorig_backend_audition_module_package_v2_utf8(
                backend, multi_port.string().c_str(), &audition_config,
                input_waveform.data(), output_waveform.data(),
                static_cast<std::uint32_t>(input_waveform.size()), &sweep_analysis) ==
            SOLO_RIG_STATUS_NOT_FOUND);
    audition_config.input_port_utf8 = "input-a";
    REQUIRE(solorig_backend_audition_module_package_v2_utf8(
                backend, multi_port.string().c_str(), &audition_config,
                input_waveform.data(), output_waveform.data(), 255U, &sweep_analysis) ==
            SOLO_RIG_STATUS_INVALID_ARGUMENT);
    audition_config.amplitude = 10.1;
    REQUIRE(solorig_backend_audition_module_package_v2_utf8(
                backend, multi_port.string().c_str(), &audition_config,
                input_waveform.data(), output_waveform.data(),
                static_cast<std::uint32_t>(input_waveform.size()), &sweep_analysis) ==
            SOLO_RIG_STATUS_INVALID_ARGUMENT);
    audition_config.amplitude = 0.2;
    audition_config.stimulus_kind = 2U;
    audition_config.start_frequency_hz = 24000.0;
    REQUIRE(solorig_backend_audition_module_package_v2_utf8(
                backend, multi_port.string().c_str(), &audition_config,
                input_waveform.data(), output_waveform.data(),
                static_cast<std::uint32_t>(input_waveform.size()), &sweep_analysis) ==
            SOLO_RIG_STATUS_INVALID_ARGUMENT);

    REQUIRE(solorig_backend_validate_module_package_utf8(backend, singular.string().c_str()) ==
            SOLO_RIG_STATUS_UNSUPPORTED);
    REQUIRE(std::string(solorig_backend_last_error_utf8(backend)).find("singular") != std::string::npos);
    REQUIRE(solorig_backend_validate_module_package_utf8(backend, unauthorized.string().c_str()) ==
            SOLO_RIG_STATUS_UNSUPPORTED);
    REQUIRE(std::string(solorig_backend_last_error_utf8(backend)).find("authorization") != std::string::npos);
    REQUIRE(solorig_backend_validate_module_package_utf8(backend, nonconvergent.string().c_str()) ==
            SOLO_RIG_STATUS_UNSUPPORTED);
    REQUIRE(std::string(solorig_backend_last_error_utf8(backend)).find("converge") != std::string::npos);
    solorig_backend_destroy(backend);
    std::filesystem::remove_all(root, cleanup);
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
