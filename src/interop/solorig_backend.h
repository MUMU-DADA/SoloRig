#pragma once

#include <stdint.h>

#if defined(_WIN32)
#if defined(SOLO_RIG_BACKEND_BUILD)
#define SOLO_RIG_BACKEND_API __declspec(dllexport)
#else
#define SOLO_RIG_BACKEND_API __declspec(dllimport)
#endif
#else
#define SOLO_RIG_BACKEND_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SoloRigBackend SoloRigBackend;
typedef struct SoloRigSnapshot SoloRigSnapshot;

typedef enum SoloRigStatus {
    SOLO_RIG_STATUS_OK = 0,
    SOLO_RIG_STATUS_INVALID_ARGUMENT = 1,
    SOLO_RIG_STATUS_OUT_OF_MEMORY = 2,
    SOLO_RIG_STATUS_NOT_FOUND = 3,
    SOLO_RIG_STATUS_FILE_ERROR = 4,
    SOLO_RIG_STATUS_INVALID_PRESET = 5,
    SOLO_RIG_STATUS_INTERNAL_ERROR = 6,
    SOLO_RIG_STATUS_UNSUPPORTED = 7,
    SOLO_RIG_STATUS_INVALID_THEME = 8,
} SoloRigStatus;

typedef struct SoloRigSnapshotInfo {
    uint32_t schema_version;
    uint8_t dark_theme;
    const char* preset_id_utf8;
    const char* preset_name_utf8;
    const char* source_path_utf8;
    const char* audio_status_utf8;
    const char* file_status_utf8;
    const char* midi_status_utf8;
    double viewport_width;
    double viewport_height;
    double dpi_scale;
    uint8_t dirty;
    uint32_t node_count;
    uint32_t connection_count;
    uint32_t binding_count;
    uint32_t total_latency_samples;
    double estimated_cpu_percent;
    const char* resource_status_utf8;
    double measured_cpu_percent;
    double peak_cpu_percent;
    uint32_t measured_node_count;
    uint8_t measurement_available;
} SoloRigSnapshotInfo;

typedef struct SoloRigModuleCatalogView {
    const char* module_id_utf8;
    const char* display_name_utf8;
    const char* category_utf8;
    const char* description_utf8;
    uint32_t parameter_count;
    uint32_t declared_latency_samples;
    double estimated_cpu_percent;
} SoloRigModuleCatalogView;

typedef struct SoloRigNodeView {
    const char* instance_id_utf8;
    const char* module_id_utf8;
    float layout_x;
    float layout_y;
    uint8_t selected;
    uint8_t unresolved;
    const char* diagnostic_utf8;
    uint32_t parameter_count;
    uint32_t declared_latency_samples;
    double estimated_cpu_percent;
    double measured_cpu_percent;
    double peak_cpu_percent;
    uint8_t measurement_available;
    uint8_t bypassed;
    const char* skin_id_utf8;
    const char* knob_skin_id_utf8;
    const char* toggle_skin_id_utf8;
    const char* button_skin_id_utf8;
    const char* footswitch_skin_id_utf8;
    const char* knob_image_path_utf8;
    const char* toggle_image_path_utf8;
    const char* button_image_path_utf8;
    const char* footswitch_image_path_utf8;
} SoloRigNodeView;

typedef struct SoloRigParameterView {
    const char* parameter_id_utf8;
    float value;
    float minimum;
    float maximum;
} SoloRigParameterView;

typedef struct SoloRigConnectionView {
    const char* source_instance_id_utf8;
    const char* destination_instance_id_utf8;
    double source_x;
    double source_y;
    double destination_x;
    double destination_y;
} SoloRigConnectionView;

typedef struct SoloRigBindingView {
    uint32_t message_type;
    uint32_t channel;
    uint32_t number;
    uint32_t target_kind;
    const char* target_id_utf8;
    float minimum;
    float maximum;
} SoloRigBindingView;

typedef struct SoloRigRealtimeContractView {
    uint32_t contract_version;
    uint32_t process_noexcept;
    uint32_t process_allocates;
    uint32_t process_reads_files;
    uint32_t process_parses_json;
    uint32_t process_locks;
    uint32_t prepare_owns_compilation;
    uint32_t maximum_oversampling;
    uint32_t sample_rate_hz;
    uint32_t max_block_size;
    uint32_t channel_count;
    uint32_t declared_latency_samples;
    double estimated_cpu_percent;
    const char* thread_rule_utf8;
} SoloRigRealtimeContractView;

typedef struct SoloRigWhiteboxStatusView {
    uint32_t schema_version;
    const char* graph_status_utf8;
    const char* runtime_status_utf8;
    const char* evidence_status_utf8;
    const char* diagnostic_utf8;
} SoloRigWhiteboxStatusView;

typedef struct SoloRigThemeInfoView {
    const char* id_utf8;
    const char* display_name_utf8;
} SoloRigThemeInfoView;

typedef struct SoloRigAsioDriverView {
    const char* driver_id_utf8;
    const char* display_name_utf8;
    uint32_t input_channel_count;
    uint32_t output_channel_count;
    double native_sample_rate_hz;
} SoloRigAsioDriverView;

typedef struct SoloRigMidiDeviceView {
    uint32_t device_index;
    const char* display_name_utf8;
} SoloRigMidiDeviceView;

typedef struct SoloRigMidiLearnTargetView {
    const char* target_id_utf8;
    const char* display_name_utf8;
    float minimum;
    float maximum;
} SoloRigMidiLearnTargetView;

// ABI contract:
// - All strings are UTF-8 and NUL-terminated. Pointers returned through a
//   snapshot remain valid until solorig_snapshot_destroy(snapshot).
// - Backend and snapshot handles are created/destroyed by this DLL. The caller
//   must release the snapshot before destroying its backend.
// - Backend calls are control/UI-thread only, are not thread-safe, and must
//   never be made from an audio callback or any real-time processing thread.
// - Startup creation only constructs the in-memory default editor. It does not
//   enumerate/open MIDI, access files, connect ASIO, or enter VST3 processing.
// - On failure, the backend state remains unchanged. Error text is UTF-8 and
//   valid until the next call on the same backend handle.
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_create(SoloRigBackend** out_backend);
SOLO_RIG_BACKEND_API void solorig_backend_destroy(SoloRigBackend* backend);
SOLO_RIG_BACKEND_API const char* solorig_backend_last_error_utf8(const SoloRigBackend* backend);
// These status views are control/UI-thread diagnostics. They do not expose
// C++ objects and do not enter the audio callback. Realtime strings are static
// UTF-8 storage owned by the DLL and remain valid for the process lifetime.
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_realtime_contract(
    SoloRigBackend* backend,
    SoloRigRealtimeContractView* out_contract);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_whitebox_status(
    SoloRigBackend* backend,
    SoloRigWhiteboxStatusView* out_status);

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_snapshot_create(
    SoloRigBackend* backend,
    SoloRigSnapshot** out_snapshot);
SOLO_RIG_BACKEND_API void solorig_snapshot_destroy(SoloRigSnapshot* snapshot);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_info(
    const SoloRigSnapshot* snapshot,
    SoloRigSnapshotInfo* out_info);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_node(
    const SoloRigSnapshot* snapshot,
    uint32_t index,
    SoloRigNodeView* out_node);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_parameter(
    const SoloRigSnapshot* snapshot,
    uint32_t node_index,
    uint32_t parameter_index,
    SoloRigParameterView* out_parameter);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_connection(
    const SoloRigSnapshot* snapshot,
    uint32_t index,
    SoloRigConnectionView* out_connection);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_binding(
    const SoloRigSnapshot* snapshot,
    uint32_t index,
    SoloRigBindingView* out_binding);

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_set_parameter_utf8(
    SoloRigBackend* backend,
    const char* instance_id_utf8,
    const char* parameter_id_utf8,
    float value);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_set_node_bypassed_utf8(
    SoloRigBackend* backend,
    const char* instance_id_utf8,
    uint8_t bypassed);
// Control/UI-thread host bridge. Values must come from an actual stopped
// runtime snapshot; this function does not perform audio processing.
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_set_node_performance_utf8(
    SoloRigBackend* backend,
    const char* instance_id_utf8,
    uint32_t declared_latency_samples,
    double measured_cpu_percent,
    double peak_cpu_percent);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_clear_node_performance(
    SoloRigBackend* backend);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_set_theme_dark(
    SoloRigBackend* backend,
    uint8_t dark);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_load_theme_utf8(
    SoloRigBackend* backend,
    const char* path_utf8);
// Theme metadata and color pointers are control-thread views. Returned
// pointers remain valid until the next operation that mutates the editor;
// callers must copy them before making another backend call.
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_theme_info(
    SoloRigBackend* backend,
    SoloRigThemeInfoView* out_theme);
SOLO_RIG_BACKEND_API const char* solorig_backend_theme_color_utf8(
    SoloRigBackend* backend,
    const char* color_key_utf8);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_set_viewport(
    SoloRigBackend* backend,
    double width,
    double height,
    double dpi_scale);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_select_at(
    SoloRigBackend* backend,
    double x,
    double y);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_begin_drag(
    SoloRigBackend* backend,
    double x,
    double y);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_drag_to(
    SoloRigBackend* backend,
    double x,
    double y,
    uint8_t snap_to_grid);
SOLO_RIG_BACKEND_API void solorig_backend_end_drag(SoloRigBackend* backend);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_save_utf8(
    SoloRigBackend* backend,
    const char* path_utf8);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_open_utf8(
    SoloRigBackend* backend,
    const char* path_utf8);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_new_preset(SoloRigBackend* backend);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_catalog_count(
    SoloRigBackend* backend,
    uint32_t* out_count);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_catalog_entry(
    SoloRigBackend* backend,
    uint32_t index,
    SoloRigModuleCatalogView* out_entry);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_add_node_utf8(
    SoloRigBackend* backend,
    const char* module_id_utf8,
    const char* instance_id_utf8,
    double layout_x,
    double layout_y);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_delete_node_utf8(
    SoloRigBackend* backend,
    const char* instance_id_utf8);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_duplicate_node_utf8(
    SoloRigBackend* backend,
    const char* source_instance_id_utf8,
    const char* new_instance_id_utf8);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_connect_utf8(
    SoloRigBackend* backend,
    const char* source_instance_id_utf8,
    const char* destination_instance_id_utf8);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_disconnect_utf8(
    SoloRigBackend* backend,
    const char* source_instance_id_utf8,
    const char* destination_instance_id_utf8);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_import_resource_utf8(
    SoloRigBackend* backend,
    const char* path_utf8);

// Control-thread-only Standalone host bridge. `native_window` is the host
// HWND used by ASIO driver dialogs/callback registration; it is never touched
// by the audio callback. Device enumeration is explicit and startup remains
// side-effect free.
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_asio_refresh(SoloRigBackend* backend);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_asio_driver_count(
    SoloRigBackend* backend, uint32_t* out_count);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_asio_driver_entry(
    SoloRigBackend* backend, uint32_t index, SoloRigAsioDriverView* out_driver);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_asio_open(
    SoloRigBackend* backend,
    uint32_t driver_index,
    void* native_window,
    uint32_t input_channel_0,
    uint32_t input_channel_1,
    uint32_t output_channel_0,
    uint32_t output_channel_1);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_asio_start(SoloRigBackend* backend);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_asio_stop(SoloRigBackend* backend);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_asio_close(SoloRigBackend* backend);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_asio_recover(SoloRigBackend* backend);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_tuner_set_enabled(
    SoloRigBackend* backend, uint8_t enabled);

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_refresh(SoloRigBackend* backend);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_device_count(
    SoloRigBackend* backend, uint32_t* out_count);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_device_entry(
    SoloRigBackend* backend, uint32_t index, SoloRigMidiDeviceView* out_device);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_open(
    SoloRigBackend* backend, uint32_t device_index);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_close(SoloRigBackend* backend);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_drain(
    SoloRigBackend* backend, uint32_t* out_processed_messages);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_target_count(
    SoloRigBackend* backend, uint32_t* out_count);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_target_entry(
    SoloRigBackend* backend, uint32_t index, SoloRigMidiLearnTargetView* out_target);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_learn_arm(
    SoloRigBackend* backend, const char* target_id_utf8);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_learn_cancel(SoloRigBackend* backend);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_delete_binding(
    SoloRigBackend* backend, uint32_t index);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_clear_bindings(SoloRigBackend* backend);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_import_bank_utf8(
    SoloRigBackend* backend, const char* directory_path_utf8);

#ifdef __cplusplus
}
#endif
