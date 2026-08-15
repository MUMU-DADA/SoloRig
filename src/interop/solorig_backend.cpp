#include "interop/solorig_backend.h"

#include "content/preset.hpp"
#include "core/realtime_contract.hpp"
#include "ui/rig_editor.hpp"

#include <cmath>
#include <filesystem>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>

struct SoloRigBackend {
    solo_rig::RigEditorViewModel editor{solo_rig::RigEditorViewModel::DefaultEditorPreset()};
    std::string last_error;
};

struct SoloRigSnapshot {
    solo_rig::RigPreset preset;
    solo_rig::RigEditorScene scene;
};

namespace {

using solo_rig::EditorFileResult;

SoloRigStatus SetError(SoloRigBackend* backend, SoloRigStatus status, std::string message) noexcept {
    if (backend != nullptr) {
        backend->last_error = std::move(message);
    }
    return status;
}

bool ValidBackend(SoloRigBackend* backend) noexcept {
    return backend != nullptr;
}

bool ValidText(const char* text) noexcept {
    return text != nullptr && *text != '\0';
}

bool ValidViewport(double width, double height, double dpi_scale) noexcept {
    return std::isfinite(width) && std::isfinite(height) && std::isfinite(dpi_scale) &&
           width > 0.0 && height > 0.0 && dpi_scale > 0.0;
}

std::filesystem::path PathFromUtf8(const char* text) {
    const auto length = std::char_traits<char>::length(text);
    std::u8string utf8;
    utf8.reserve(length);
    for (std::size_t index = 0; index < length; ++index) {
        utf8.push_back(static_cast<char8_t>(text[index]));
    }
    return std::filesystem::path(std::move(utf8));
}

SoloRigStatus MapFileResult(SoloRigBackend* backend, const EditorFileResult& result) noexcept {
    if (result.success) {
        backend->last_error.clear();
        return SOLO_RIG_STATUS_OK;
    }
    if (!result.error.has_value()) {
        return SetError(backend, SOLO_RIG_STATUS_INTERNAL_ERROR, result.message);
    }
    switch (*result.error) {
    case solo_rig::EditorFileError::InvalidExtension:
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, result.message);
    case solo_rig::EditorFileError::CannotRead:
    case solo_rig::EditorFileError::CannotWrite:
        return SetError(backend, SOLO_RIG_STATUS_FILE_ERROR, result.message);
    case solo_rig::EditorFileError::InvalidJson:
    case solo_rig::EditorFileError::InvalidSchema:
    case solo_rig::EditorFileError::InvalidTopology:
        return SetError(backend, SOLO_RIG_STATUS_INVALID_PRESET, result.message);
    }
    return SetError(backend, SOLO_RIG_STATUS_INTERNAL_ERROR, result.message);
}

const solo_rig::PresetNode* FindPresetNode(const SoloRigSnapshot* snapshot, uint32_t index) noexcept {
    if (snapshot == nullptr || index >= snapshot->preset.nodes.size()) {
        return nullptr;
    }
    return &snapshot->preset.nodes[index];
}

const solo_rig::RigEditorNode* FindSceneNode(const SoloRigSnapshot* snapshot, uint32_t index) noexcept {
    if (snapshot == nullptr || index >= snapshot->scene.nodes.size()) {
        return nullptr;
    }
    return &snapshot->scene.nodes[index];
}

} // namespace

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_create(SoloRigBackend** out_backend) {
    if (out_backend == nullptr) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    *out_backend = nullptr;
    try {
        *out_backend = new SoloRigBackend{};
        return SOLO_RIG_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return SOLO_RIG_STATUS_OUT_OF_MEMORY;
    } catch (...) {
        return SOLO_RIG_STATUS_INTERNAL_ERROR;
    }
}

SOLO_RIG_BACKEND_API void solorig_backend_destroy(SoloRigBackend* backend) {
    delete backend;
}

SOLO_RIG_BACKEND_API const char* solorig_backend_last_error_utf8(const SoloRigBackend* backend) {
    return backend == nullptr ? "invalid backend handle" : backend->last_error.c_str();
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_realtime_contract(
    SoloRigBackend* backend,
    SoloRigRealtimeContractView* out_contract) {
    if (!ValidBackend(backend) || out_contract == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                        "backend and output realtime contract are required");
    }
    constexpr auto contract = solo_rig::DefaultRealtimeContract();
    static constexpr char kThreadRule[] =
        "Prepare/control thread owns compilation; audio Process is C++ only and never enters .NET";
    out_contract->contract_version = 1U;
    out_contract->process_noexcept = contract.process_noexcept ? 1U : 0U;
    out_contract->process_allocates = contract.process_allocates ? 1U : 0U;
    out_contract->process_reads_files = contract.process_reads_files ? 1U : 0U;
    out_contract->process_parses_json = contract.process_parses_json ? 1U : 0U;
    out_contract->process_locks = contract.process_locks ? 1U : 0U;
    out_contract->prepare_owns_compilation = contract.prepare_owns_compilation ? 1U : 0U;
    out_contract->maximum_oversampling = contract.maximum_oversampling;
    // The editor backend is not an audio device. A host/standalone audio
    // adapter fills these values when it prepares a real runtime.
    out_contract->sample_rate_hz = contract.sample_rate_hz;
    out_contract->max_block_size = contract.max_block_size;
    out_contract->channel_count = contract.channel_count;
    out_contract->declared_latency_samples = contract.declared_latency_samples;
    out_contract->estimated_cpu_percent = contract.estimated_cpu_percent;
    out_contract->thread_rule_utf8 = kThreadRule;
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_whitebox_status(
    SoloRigBackend* backend,
    SoloRigWhiteboxStatusView* out_status) {
    if (!ValidBackend(backend) || out_status == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                        "backend and output whitebox status are required");
    }
    static constexpr char kGraphStatus[] = "M12 immutable circuit IR contract available";
    static constexpr char kRuntimeStatus[] =
        "M13 prepared runtime has a validated subset; WDF/full generic compiler remains unsupported";
    static constexpr char kEvidenceStatus[] =
        "M16 evidence schema is schema-only/blocked unless authorized inputs exist";
    static constexpr char kDiagnostic[] =
        "PedalKernel source/models and SPICE/golden data are unavailable or unauthorized; no equivalence claim";
    out_status->schema_version = 1U;
    out_status->graph_status_utf8 = kGraphStatus;
    out_status->runtime_status_utf8 = kRuntimeStatus;
    out_status->evidence_status_utf8 = kEvidenceStatus;
    out_status->diagnostic_utf8 = kDiagnostic;
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_snapshot_create(
    SoloRigBackend* backend,
    SoloRigSnapshot** out_snapshot) {
    if (!ValidBackend(backend) || out_snapshot == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "backend and output snapshot are required");
    }
    *out_snapshot = nullptr;
    try {
        auto snapshot = std::make_unique<SoloRigSnapshot>();
        snapshot->preset = backend->editor.ExportPreset();
        snapshot->scene = backend->editor.Scene();
        *out_snapshot = snapshot.release();
        backend->last_error.clear();
        return SOLO_RIG_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return SetError(backend, SOLO_RIG_STATUS_OUT_OF_MEMORY, "snapshot allocation failed");
    } catch (...) {
        return SetError(backend, SOLO_RIG_STATUS_INTERNAL_ERROR, "snapshot creation failed");
    }
}

SOLO_RIG_BACKEND_API void solorig_snapshot_destroy(SoloRigSnapshot* snapshot) {
    delete snapshot;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_info(
    const SoloRigSnapshot* snapshot,
    SoloRigSnapshotInfo* out_info) {
    if (snapshot == nullptr || out_info == nullptr) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    out_info->schema_version = 3U;
    out_info->dark_theme = snapshot->scene.theme.id.find("dark") != std::string::npos ? 1U : 0U;
    out_info->preset_id_utf8 = snapshot->preset.preset_id.c_str();
    out_info->preset_name_utf8 = snapshot->preset.name.c_str();
    out_info->source_path_utf8 = snapshot->scene.source_path.c_str();
    out_info->audio_status_utf8 = snapshot->scene.audio_status.c_str();
    out_info->file_status_utf8 = snapshot->scene.file_status.c_str();
    static constexpr char kMidiClosed[] = "MIDI closed; no device access on startup";
    out_info->midi_status_utf8 = kMidiClosed;
    out_info->viewport_width = snapshot->scene.viewport_width;
    out_info->viewport_height = snapshot->scene.viewport_height;
    out_info->dpi_scale = snapshot->scene.dpi_scale;
    out_info->dirty = snapshot->scene.dirty ? 1U : 0U;
    out_info->node_count = static_cast<uint32_t>(snapshot->scene.nodes.size());
    out_info->connection_count = static_cast<uint32_t>(snapshot->scene.connections.size());
    out_info->binding_count = snapshot->preset.midi_bindings.has_value()
        ? static_cast<uint32_t>(snapshot->preset.midi_bindings->size()) : 0U;
    out_info->total_latency_samples = snapshot->scene.performance.total_latency_samples;
    out_info->estimated_cpu_percent = snapshot->scene.performance.estimated_cpu_percent;
    out_info->resource_status_utf8 = snapshot->scene.resource_status.c_str();
    out_info->measured_cpu_percent = snapshot->scene.performance.measured_cpu_percent;
    out_info->peak_cpu_percent = snapshot->scene.performance.peak_cpu_percent;
    out_info->measured_node_count = snapshot->scene.performance.measured_node_count;
    out_info->measurement_available = snapshot->scene.performance.measurement_available ? 1U : 0U;
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_node(
    const SoloRigSnapshot* snapshot,
    uint32_t index,
    SoloRigNodeView* out_node) {
    const auto* node = FindSceneNode(snapshot, index);
    const auto* preset_node = FindPresetNode(snapshot, index);
    if (node == nullptr || preset_node == nullptr || out_node == nullptr) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    out_node->instance_id_utf8 = node->instance_id.c_str();
    out_node->module_id_utf8 = node->module_id.c_str();
    out_node->layout_x = static_cast<float>(node->bounds.x);
    out_node->layout_y = static_cast<float>(node->bounds.y);
    out_node->selected = node->selected ? 1U : 0U;
    out_node->unresolved = node->unresolved ? 1U : 0U;
    out_node->diagnostic_utf8 = node->diagnostic.c_str();
    out_node->parameter_count = static_cast<uint32_t>(preset_node->parameter_values.size());
    out_node->declared_latency_samples = node->declared_latency_samples;
    out_node->estimated_cpu_percent = node->estimated_cpu_percent;
    out_node->measured_cpu_percent = node->measured_cpu_percent;
    out_node->peak_cpu_percent = node->peak_cpu_percent;
    out_node->measurement_available = node->measurement_available ? 1U : 0U;
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_parameter(
    const SoloRigSnapshot* snapshot,
    uint32_t node_index,
    uint32_t parameter_index,
    SoloRigParameterView* out_parameter) {
    const auto* node = FindPresetNode(snapshot, node_index);
    if (node == nullptr || out_parameter == nullptr || parameter_index >= node->parameter_values.size()) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    auto iterator = node->parameter_values.begin();
    std::advance(iterator, parameter_index);
    out_parameter->parameter_id_utf8 = iterator->first.c_str();
    out_parameter->value = iterator->second;
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_connection(
    const SoloRigSnapshot* snapshot,
    uint32_t index,
    SoloRigConnectionView* out_connection) {
    if (snapshot == nullptr || out_connection == nullptr || index >= snapshot->scene.connections.size()) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    const auto& connection = snapshot->scene.connections[index];
    out_connection->source_instance_id_utf8 = connection.source_instance_id.c_str();
    out_connection->destination_instance_id_utf8 = connection.destination_instance_id.c_str();
    out_connection->source_x = connection.source.x;
    out_connection->source_y = connection.source.y;
    out_connection->destination_x = connection.destination.x;
    out_connection->destination_y = connection.destination.y;
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_binding(
    const SoloRigSnapshot* snapshot,
    uint32_t index,
    SoloRigBindingView* out_binding) {
    if (snapshot == nullptr || out_binding == nullptr || !snapshot->preset.midi_bindings.has_value() ||
        index >= snapshot->preset.midi_bindings->size()) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    const auto& binding = (*snapshot->preset.midi_bindings)[index];
    out_binding->message_type = static_cast<uint32_t>(binding.message_type);
    out_binding->channel = binding.channel;
    out_binding->number = binding.number;
    out_binding->target_kind = static_cast<uint32_t>(binding.target_kind);
    out_binding->target_id_utf8 = binding.target_id.c_str();
    out_binding->minimum = binding.minimum;
    out_binding->maximum = binding.maximum;
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_set_parameter_utf8(
    SoloRigBackend* backend,
    const char* instance_id_utf8,
    const char* parameter_id_utf8,
    float value) {
    if (!ValidBackend(backend) || !ValidText(instance_id_utf8) || !ValidText(parameter_id_utf8) || !std::isfinite(value)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "valid parameter identifiers and finite value are required");
    }
    if (!backend->editor.SetNodeParameter(instance_id_utf8, parameter_id_utf8, value)) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "parameter target was not found or value was rejected");
    }
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_set_node_performance_utf8(
    SoloRigBackend* backend,
    const char* instance_id_utf8,
    uint32_t declared_latency_samples,
    double measured_cpu_percent,
    double peak_cpu_percent) {
    if (!ValidBackend(backend) || !ValidText(instance_id_utf8) ||
        !std::isfinite(measured_cpu_percent) || !std::isfinite(peak_cpu_percent) ||
        measured_cpu_percent < 0.0 || peak_cpu_percent < measured_cpu_percent) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                        "node performance requires finite non-negative values with peak >= last");
    }
    if (!backend->editor.SetMeasuredPerformance(instance_id_utf8,
                                                declared_latency_samples,
                                                measured_cpu_percent,
                                                peak_cpu_percent)) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "performance node was not found");
    }
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_clear_node_performance(
    SoloRigBackend* backend) {
    if (!ValidBackend(backend)) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    backend->editor.ClearMeasuredPerformance();
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_set_theme_dark(
    SoloRigBackend* backend,
    uint8_t dark) {
    if (!ValidBackend(backend) || dark > 1U) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "theme mode must be 0 or 1");
    }
    backend->editor.SetThemeMode(dark != 0U);
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_set_viewport(
    SoloRigBackend* backend,
    double width,
    double height,
    double dpi_scale) {
    if (!ValidBackend(backend) || !ValidViewport(width, height, dpi_scale)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "finite positive viewport and DPI are required");
    }
    backend->editor.SetViewport(width, height, dpi_scale);
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_select_at(
    SoloRigBackend* backend,
    double x,
    double y) {
    if (!ValidBackend(backend) || !std::isfinite(x) || !std::isfinite(y)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "finite editor coordinates are required");
    }
    if (!backend->editor.SelectAt({.x = x, .y = y})) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "no node was found at the editor coordinate");
    }
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_begin_drag(
    SoloRigBackend* backend,
    double x,
    double y) {
    if (!ValidBackend(backend) || !std::isfinite(x) || !std::isfinite(y)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "finite editor coordinates are required");
    }
    if (!backend->editor.BeginDrag({.x = x, .y = y})) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "no node was found at the editor coordinate");
    }
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_drag_to(
    SoloRigBackend* backend,
    double x,
    double y,
    uint8_t snap_to_grid) {
    if (!ValidBackend(backend) || snap_to_grid > 1U || !std::isfinite(x) || !std::isfinite(y)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "finite coordinates and a 0/1 snap flag are required");
    }
    if (!backend->editor.DragTo({.x = x, .y = y}, snap_to_grid != 0U)) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "no active node drag exists");
    }
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API void solorig_backend_end_drag(SoloRigBackend* backend) {
    if (ValidBackend(backend)) {
        backend->editor.EndDrag();
        backend->last_error.clear();
    }
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_save_utf8(
    SoloRigBackend* backend,
    const char* path_utf8) {
    if (!ValidBackend(backend) || !ValidText(path_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "a UTF-8 .srig path is required");
    }
    return MapFileResult(backend, backend->editor.SaveTo(PathFromUtf8(path_utf8)));
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_open_utf8(
    SoloRigBackend* backend,
    const char* path_utf8) {
    if (!ValidBackend(backend) || !ValidText(path_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "a UTF-8 .srig path is required");
    }
    return MapFileResult(backend, backend->editor.LoadFrom(PathFromUtf8(path_utf8)));
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_new_preset(SoloRigBackend* backend) {
    if (!ValidBackend(backend)) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    backend->editor.NewPreset();
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_catalog_count(
    SoloRigBackend* backend, uint32_t* out_count) {
    if (!ValidBackend(backend) || out_count == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "backend and catalog count are required");
    }
    const auto& catalog = backend->editor.Catalog();
    *out_count = static_cast<uint32_t>(catalog.size());
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_catalog_entry(
    SoloRigBackend* backend, uint32_t index, SoloRigModuleCatalogView* out_entry) {
    if (!ValidBackend(backend) || out_entry == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "backend and catalog entry are required");
    }
    const auto& catalog = backend->editor.Catalog();
    if (index >= catalog.size()) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "module catalog entry was not found");
    }
    const auto& entry = catalog[index];
    out_entry->module_id_utf8 = entry.module_id.c_str();
    out_entry->display_name_utf8 = entry.display_name.c_str();
    out_entry->category_utf8 = entry.category.c_str();
    out_entry->description_utf8 = entry.description.c_str();
    out_entry->parameter_count = static_cast<uint32_t>(entry.default_parameters.size());
    out_entry->declared_latency_samples = entry.latency_samples;
    out_entry->estimated_cpu_percent = entry.estimated_cpu_percent;
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_add_node_utf8(
    SoloRigBackend* backend, const char* module_id_utf8, const char* instance_id_utf8,
    double layout_x, double layout_y) {
    if (!ValidBackend(backend) || !ValidText(module_id_utf8) || !ValidText(instance_id_utf8) ||
        !std::isfinite(layout_x) || !std::isfinite(layout_y)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "module, instance and finite layout are required");
    }
    if (!backend->editor.AddNode(module_id_utf8, instance_id_utf8, layout_x, layout_y)) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "module node could not be added");
    }
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_delete_node_utf8(
    SoloRigBackend* backend, const char* instance_id_utf8) {
    if (!ValidBackend(backend) || !ValidText(instance_id_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "a node instance ID is required");
    }
    if (!backend->editor.DeleteNode(instance_id_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "node could not be deleted");
    }
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_duplicate_node_utf8(
    SoloRigBackend* backend, const char* source_instance_id_utf8, const char* new_instance_id_utf8) {
    if (!ValidBackend(backend) || !ValidText(source_instance_id_utf8) || !ValidText(new_instance_id_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "source and destination node IDs are required");
    }
    if (!backend->editor.DuplicateNode(source_instance_id_utf8, new_instance_id_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "node could not be duplicated");
    }
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_connect_utf8(
    SoloRigBackend* backend, const char* source_instance_id_utf8, const char* destination_instance_id_utf8) {
    if (!ValidBackend(backend) || !ValidText(source_instance_id_utf8) || !ValidText(destination_instance_id_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "source and destination node IDs are required");
    }
    if (!backend->editor.ConnectNodes(source_instance_id_utf8, destination_instance_id_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_PRESET, "connection was rejected by topology validation");
    }
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_disconnect_utf8(
    SoloRigBackend* backend, const char* source_instance_id_utf8, const char* destination_instance_id_utf8) {
    if (!ValidBackend(backend) || !ValidText(source_instance_id_utf8) || !ValidText(destination_instance_id_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "source and destination node IDs are required");
    }
    if (!backend->editor.DisconnectNodes(source_instance_id_utf8, destination_instance_id_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "connection was not found");
    }
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_import_resource_utf8(
    SoloRigBackend* backend, const char* path_utf8) {
    if (!ValidBackend(backend) || !ValidText(path_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "an explicit resource path is required");
    }
    const auto result = backend->editor.ImportResource(PathFromUtf8(path_utf8));
    if (!result.success) {
        if (result.error == solo_rig::EditorFileError::CannotRead) {
            return SetError(backend, SOLO_RIG_STATUS_FILE_ERROR, result.message);
        }
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, result.message);
    }
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}
