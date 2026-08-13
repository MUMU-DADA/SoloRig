#include "interop/solorig_backend.h"

#include "content/preset.hpp"
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
    out_info->schema_version = 2U;
    out_info->dark_theme = snapshot->scene.theme.id.find("dark") != std::string::npos ? 1U : 0U;
    out_info->preset_id_utf8 = snapshot->preset.preset_id.c_str();
    out_info->preset_name_utf8 = snapshot->preset.name.c_str();
    out_info->source_path_utf8 = snapshot->scene.source_path.c_str();
    out_info->audio_status_utf8 = snapshot->scene.audio_status.c_str();
    out_info->file_status_utf8 = snapshot->scene.file_status.c_str();
    static constexpr char kMidiClosed[] = "MIDI closed; no device access on startup";
    out_info->midi_status_utf8 = kMidiClosed;
    out_info->node_count = static_cast<uint32_t>(snapshot->scene.nodes.size());
    out_info->binding_count = snapshot->preset.midi_bindings.has_value()
        ? static_cast<uint32_t>(snapshot->preset.midi_bindings->size()) : 0U;
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
