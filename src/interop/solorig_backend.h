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

// Additive control-plane view for ASIO lifecycle state. Kept separate from
// SoloRigSnapshotInfo so existing C ABI consumers retain their struct size.
typedef struct SoloRigAudioStateView {
    uint8_t is_open;
    uint8_t is_running;
    uint8_t reset_requested;
    uint8_t dropout_reported;
    double sample_rate_changed_to;
} SoloRigAudioStateView;

// Additive control-plane view for Standalone tuner state. Kept separate from
// SoloRigSnapshotInfo so existing C ABI consumers retain their struct size.
typedef struct SoloRigTunerStateView {
    uint8_t enabled;
    uint8_t signal_present;
    uint8_t reserved0;
    uint8_t reserved1;
    int32_t midi_note;
    double frequency_hz;
    double cents_from_nearest_note;
    double confidence;
} SoloRigTunerStateView;

// Additive control-plane view for MIDI lifecycle state. Kept separate from
// SoloRigSnapshotInfo so existing C ABI consumers retain their struct size.
typedef struct SoloRigMidiStateView {
    uint8_t is_open;
    uint8_t learn_armed;
    uint8_t state;
    uint8_t reserved;
    uint32_t selected_device_index;
    uint64_t dropped_message_count;
} SoloRigMidiStateView;

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

// Additive node inspection views. They are separate from SoloRigNodeView so
// existing C ABI consumers retain their struct size and field offsets.
typedef struct SoloRigNodePortView {
    const char* port_id_utf8;
    uint8_t direction;
    uint32_t max_channels;
} SoloRigNodePortView;

typedef struct SoloRigNodeResourceView {
    const char* kind_utf8;
    const char* path_utf8;
    const char* status_utf8;
} SoloRigNodeResourceView;

typedef struct SoloRigNodeSkinLayoutView {
    double width;
    double height;
    const char* background_color_utf8;
    const char* label_color_utf8;
    uint8_t has_footswitch_position;
    double footswitch_x;
    double footswitch_y;
} SoloRigNodeSkinLayoutView;

typedef struct SoloRigNodeSkinParameterLayoutView {
    const char* parameter_id_utf8;
    double x;
    double y;
} SoloRigNodeSkinParameterLayoutView;

typedef struct SoloRigNodeBatchEntry {
    const char* instance_id_utf8;
} SoloRigNodeBatchEntry;

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

typedef struct SoloRigModuleAuditionView {
    uint32_t sample_rate_hz;
    uint32_t block_size;
    uint32_t channel_count;
    uint32_t declared_latency_samples;
    double output_peak;
    double output_rms;
    double processing_cpu_percent;
    uint8_t output_finite;
} SoloRigModuleAuditionView;

typedef struct SoloRigModuleAuditionConfigV2 {
    uint32_t sample_rate_hz;
    uint32_t block_size;
    uint32_t channel_count;
    uint32_t stimulus_kind; // 0=sine, 1=impulse, 2=log sweep, 3=caller waveform
    const char* input_port_utf8;
    const char* output_port_utf8;
    double amplitude;
    double start_frequency_hz;
    double end_frequency_hz;
} SoloRigModuleAuditionConfigV2;

typedef struct SoloRigModuleAuditionAnalysisV2 {
    uint32_t schema_version;
    uint32_t sample_rate_hz;
    uint32_t block_size;
    uint32_t channel_count;
    uint32_t declared_latency_samples;
    uint32_t waveform_sample_count;
    double input_peak;
    double input_rms;
    double input_dc;
    double output_peak;
    double output_rms;
    double output_dc;
    double processing_cpu_percent;
    uint8_t output_finite;
} SoloRigModuleAuditionAnalysisV2;

typedef struct SoloRigModuleAuditionAnalysisV3 {
    uint32_t schema_version;
    uint32_t sample_rate_hz;
    uint32_t block_size;
    uint32_t channel_count;
    uint32_t declared_latency_samples;
    uint32_t waveform_sample_count;
    double input_peak;
    double input_rms;
    double input_dc;
    double output_peak;
    double output_rms;
    double output_dc;
    double processing_cpu_percent;
    uint8_t output_finite;
    uint64_t solver_processed_sample_count;
    uint64_t solver_total_iterations;
    uint32_t solver_maximum_iterations;
    uint32_t solver_failure_count;
    uint32_t solver_last_error;
} SoloRigModuleAuditionAnalysisV3;

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

// One physical ASIO channel mapped into the fixed internal processing graph.
// The additive routed-open entry point accepts independent input/output maps,
// so different physical devices can be active at the same time.
typedef struct SoloRigAsioChannelRoute {
    uint32_t driver_index;
    uint32_t device_channel;
    uint32_t processing_channel;
} SoloRigAsioChannelRoute;

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
SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_audio_state(
    const SoloRigSnapshot* snapshot,
    SoloRigAudioStateView* out_state);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_tuner_state(
    const SoloRigSnapshot* snapshot,
    SoloRigTunerStateView* out_state);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_midi_state(
    const SoloRigSnapshot* snapshot,
    SoloRigMidiStateView* out_state);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_node(
    const SoloRigSnapshot* snapshot,
    uint32_t index,
    SoloRigNodeView* out_node);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_node_port_count(
    const SoloRigSnapshot* snapshot,
    uint32_t node_index,
    uint32_t* out_count);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_node_port(
    const SoloRigSnapshot* snapshot,
    uint32_t node_index,
    uint32_t port_index,
    SoloRigNodePortView* out_port);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_node_resource(
    const SoloRigSnapshot* snapshot,
    uint32_t node_index,
    SoloRigNodeResourceView* out_resource);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_node_skin_layout(
    const SoloRigSnapshot* snapshot,
    uint32_t node_index,
    SoloRigNodeSkinLayoutView* out_layout);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_node_skin_parameter_layout_count(
    const SoloRigSnapshot* snapshot,
    uint32_t node_index,
    uint32_t* out_count);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_node_skin_parameter_layout(
    const SoloRigSnapshot* snapshot,
    uint32_t node_index,
    uint32_t layout_index,
    SoloRigNodeSkinParameterLayoutView* out_layout);
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
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_set_parameter_utf8_with_dirty(
    SoloRigBackend* backend,
    const char* instance_id_utf8,
    const char* parameter_id_utf8,
    float value,
    uint8_t mark_dirty);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_set_node_bypassed_utf8(
    SoloRigBackend* backend,
    const char* instance_id_utf8,
    uint8_t bypassed);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_set_nodes_bypassed(
    SoloRigBackend* backend,
    const SoloRigNodeBatchEntry* nodes,
    uint32_t node_count,
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
// Control-thread transaction helper: rebuilds the current document snapshot
// while preserving its preset, theme and source path, with an explicit dirty state.
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_restore_document_state(
    SoloRigBackend* backend,
    uint8_t dirty);
SOLO_RIG_BACKEND_API uint8_t solorig_backend_can_undo(const SoloRigBackend* backend);
SOLO_RIG_BACKEND_API uint8_t solorig_backend_can_redo(const SoloRigBackend* backend);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_undo(SoloRigBackend* backend);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_redo(SoloRigBackend* backend);
// Control/test helper used when a probe restores the document without
// retaining the probe's transient edit history.
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_clear_history(SoloRigBackend* backend);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_set_theme_dark(
    SoloRigBackend* backend,
    uint8_t dark);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_set_theme_dark_with_dirty(
    SoloRigBackend* backend,
    uint8_t dark,
    uint8_t mark_dirty);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_load_theme_utf8(
    SoloRigBackend* backend,
    const char* path_utf8);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_load_theme_utf8_with_dirty(
    SoloRigBackend* backend,
    const char* path_utf8,
    uint8_t mark_dirty);
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
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_clear_selection(
    SoloRigBackend* backend);
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
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_add_node_to_chain_utf8(
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
// Validates a package directory without importing it or changing editor state.
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_validate_module_package_utf8(
    SoloRigBackend* backend,
    const char* directory_path_utf8);
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_audition_module_package_utf8(
    SoloRigBackend* backend,
    const char* directory_path_utf8,
    SoloRigModuleAuditionView* out_audition);
// Configurable offline audition. Waveform buffers are caller-owned and must
// hold at least config.block_size floats. Port IDs select named Circuit v2
// boundaries; empty strings select the first input/output for legacy modules.
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_audition_module_package_v2_utf8(
    SoloRigBackend* backend,
    const char* directory_path_utf8,
    const SoloRigModuleAuditionConfigV2* config,
    float* input_waveform,
    float* output_waveform,
    uint32_t waveform_capacity,
    SoloRigModuleAuditionAnalysisV2* out_analysis);
// Extends v2 with solver convergence diagnostics. Non-MNA modules report zero
// solver counters while retaining the same waveform and signal analysis.
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_audition_module_package_v3_utf8(
    SoloRigBackend* backend,
    const char* directory_path_utf8,
    const SoloRigModuleAuditionConfigV2* config,
    float* input_waveform,
    float* output_waveform,
    uint32_t waveform_capacity,
    SoloRigModuleAuditionAnalysisV3* out_analysis);
// Parameterized offline audition used by sweep and A/B tools. The parameter
// must be declared by the package and accepted by the prepared runtime.
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_audition_module_package_v4_utf8(
    SoloRigBackend* backend,
    const char* directory_path_utf8,
    const SoloRigModuleAuditionConfigV2* config,
    const char* parameter_id_utf8,
    double parameter_value,
    float* input_waveform,
    float* output_waveform,
    uint32_t waveform_capacity,
    SoloRigModuleAuditionAnalysisV3* out_analysis);

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
SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_asio_open_routed(
    SoloRigBackend* backend,
    void* native_window,
    const SoloRigAsioChannelRoute* input_routes,
    uint32_t input_route_count,
    const SoloRigAsioChannelRoute* output_routes,
    uint32_t output_route_count);
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
