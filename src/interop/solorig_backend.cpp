#include "interop/solorig_backend.h"

#include "content/module_package.hpp"
#include "content/module_runtime.hpp"
#include "content/preset.hpp"
#include "core/realtime_contract.hpp"
#include "dsp/mna_solver.hpp"
#include "standalone/asio_host.hpp"
#include "standalone/midi_control_session.hpp"
#include "standalone/standalone_audio_engine.hpp"
#include "ui/rig_editor.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct EditorHistoryState {
    solo_rig::RigPreset preset;
    std::optional<std::filesystem::path> source_path;
    bool dirty{};
    std::string file_status;
    std::string resource_status;
};

struct SoloRigBackend {
    solo_rig::RigEditorViewModel editor{solo_rig::RigEditorViewModel::DefaultEditorPreset()};
    std::unique_ptr<solo_rig::standalone::StandaloneAudioEngine> audio_engine;
    std::unique_ptr<solo_rig::standalone::StandaloneMidiControlSession> midi_session;
    std::vector<std::string> midi_device_names_utf8;
    std::vector<solo_rig::standalone::MidiLearnTarget> midi_learn_targets;
    std::vector<EditorHistoryState> undo_history;
    std::vector<EditorHistoryState> redo_history;
    std::optional<EditorHistoryState> pending_drag_history;
    std::string last_error;
};

struct SoloRigSnapshot {
    struct NodePort {
        std::string id;
        std::uint8_t direction{};
        std::uint32_t max_channels{};
    };
    struct NodeResource {
        std::string kind;
        std::string path;
        std::string status;
    };
    solo_rig::RigPreset preset;
    solo_rig::RigEditorScene scene;
    std::string midi_status;
    std::vector<std::vector<std::pair<float, float>>> parameter_ranges;
    std::vector<std::array<std::string, 4>> node_skin_images_utf8;
    std::vector<std::vector<NodePort>> node_ports;
    std::vector<NodeResource> node_resources;
    bool audio_open{};
    bool audio_running{};
    bool audio_reset_requested{};
    bool audio_dropout_reported{};
    double audio_sample_rate_changed_to{};
    bool tuner_enabled{};
    bool tuner_signal_present{};
    std::int32_t tuner_midi_note{-1};
    double tuner_frequency_hz{};
    double tuner_cents_from_nearest_note{};
    double tuner_confidence{};
    bool midi_open{};
    bool midi_learn_armed{};
    std::uint8_t midi_state{};
    std::uint32_t midi_selected_device_index{};
    std::uint64_t midi_dropped_message_count{};
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

std::string WideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const auto length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                            static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), result.data(), length, nullptr, nullptr) <= 0) {
        return {};
    }
    return result;
}

void SyncAudioStatus(SoloRigBackend& backend) {
    if (backend.audio_engine != nullptr) {
        backend.editor.SetAudioStatus(backend.audio_engine->RuntimeStatusText());
    }
}

void SyncMidiStatus(SoloRigBackend& backend) {
    if (backend.midi_session != nullptr) {
        backend.midi_device_names_utf8.clear();
        backend.midi_device_names_utf8.reserve(backend.midi_session->Devices().size());
        for (const auto& device : backend.midi_session->Devices()) {
            backend.midi_device_names_utf8.push_back(WideToUtf8(device.display_name));
        }
    }
}

void SyncMidiLearnTargets(SoloRigBackend& backend) {
    if (backend.midi_session != nullptr) {
        backend.midi_learn_targets = backend.midi_session->ParameterTargets();
    } else {
        backend.midi_learn_targets.clear();
    }
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

std::string PathToUtf8(const std::optional<std::filesystem::path>& path) {
    if (!path.has_value()) {
        return {};
    }
    const auto text = path->u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
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

constexpr std::size_t kMaximumEditorHistory = 64U;

EditorHistoryState CaptureEditorHistory(const SoloRigBackend& backend) {
    return {
        .preset = backend.editor.ExportPreset(),
        .source_path = backend.editor.SourcePath(),
        .dirty = backend.editor.IsDirty(),
        .file_status = std::string(backend.editor.FileStatus()),
        .resource_status = backend.editor.Scene().resource_status,
    };
}

void PushEditorHistory(std::vector<EditorHistoryState>& history, EditorHistoryState state) {
    if (history.size() >= kMaximumEditorHistory) {
        history.erase(history.begin());
    }
    history.push_back(std::move(state));
}

std::string_view ModulePackageErrorText(solo_rig::ModulePackageLoadError error) {
    using enum solo_rig::ModulePackageLoadError;
    switch (error) {
    case MissingManifest: return "manifest.json is missing";
    case CannotReadManifest: return "manifest.json cannot be read";
    case InvalidJson: return "manifest.json is not valid JSON";
    case InvalidSchema: return "module package schema is unsupported";
    case MissingRequiredField: return "module package is missing a required field";
    case InvalidField: return "module package contains an invalid field";
    case UnsafeAssetPath: return "module package contains an unsafe asset path";
    case DescriptorInvalid: return "module metadata, ports, parameters, or latency are invalid";
    case MissingLogicFile: return "module logic file is missing";
    case CannotReadLogicFile: return "module logic file cannot be read";
    case InvalidLogicJson: return "module logic file is not valid JSON";
    case InvalidLogicSchema: return "module logic schema is unsupported";
    case LogicCategoryMismatch: return "module category and logic kind do not match";
    case InvalidLogicDefinition: return "module logic definition is invalid or unsupported";
    case LogicParameterUnknown: return "module logic references an undeclared parameter";
    case ExternalAssetInvalid: return "external module asset is invalid";
    }
    return "module package validation failed";
}

std::string_view ModuleRuntimeErrorText(solo_rig::ModuleRuntimeError error) {
    using enum solo_rig::ModuleRuntimeError;
    switch (error) {
    case MissingLogic: return "module runtime logic is missing";
    case LogicCategoryMismatch: return "module runtime category does not match its logic";
    case UnsupportedCategory: return "module runtime category is unsupported";
    case UnsupportedAnalogTopology: return "analog topology is unsupported by the runtime";
    case UnsupportedDigitalOperation: return "digital operation is unsupported by the runtime";
    case InvalidDigitalTopology: return "digital operation topology is invalid";
    case InvalidInitialParameterValue: return "initial parameter value is invalid";
    case UnsupportedPortLayout: return "module port layout is unsupported by the audition runtime";
    case UnsupportedCircuit: return "circuit cannot be prepared by the runtime";
    case ExternalAssetLoadFailed: return "external model or impulse response could not be loaded";
    }
    return "module runtime creation failed";
}

std::string_view MnaCompileErrorText(solo_rig::MnaCompileError error) {
    using enum solo_rig::MnaCompileError;
    switch (error) {
    case InvalidTopology: return "circuit topology is invalid";
    case UnsupportedComponent: return "circuit contains an unsupported component";
    case InvalidValue: return "circuit contains an invalid component value";
    case InvalidSolver: return "circuit solver settings are invalid";
    case InvalidModel: return "circuit component model is invalid";
    case MissingModelAuthorization: return "circuit model reference requires ModelDatabase authorization";
    case InvalidConvergence: return "circuit convergence settings are invalid";
    }
    return "circuit compilation failed";
}

std::string_view MnaSolveErrorText(solo_rig::MnaSolveError error) {
    using enum solo_rig::MnaSolveError;
    switch (error) {
    case None: return "";
    case NotPrepared: return "circuit was not prepared";
    case SingularMatrix: return "circuit matrix is singular";
    case NonFinite: return "circuit solve produced a non-finite value";
    case NonConvergent: return "circuit solver did not converge";
    case InvalidInputCount: return "circuit input port count is invalid";
    }
    return "circuit solve failed";
}

void FillAuditionStimulus(solo_rig::AudioBuffer& buffer,
                          const SoloRigModuleAuditionConfigV2& config,
                          const float* caller_waveform) {
    constexpr double two_pi = 6.28318530717958647692;
    for (std::uint32_t channel = 0U; channel < config.channel_count; ++channel) {
        auto* samples = buffer.ChannelData(channel);
        double phase = 0.0;
        for (std::uint32_t frame = 0U; frame < config.block_size; ++frame) {
            double sample = 0.0;
            if (config.stimulus_kind == 3U) {
                sample = caller_waveform[frame];
            } else if (config.stimulus_kind == 1U) {
                sample = frame == 0U ? config.amplitude : 0.0;
            } else if (config.stimulus_kind == 2U) {
                const auto progress = config.block_size > 1U
                    ? static_cast<double>(frame) / static_cast<double>(config.block_size - 1U) : 0.0;
                const auto frequency = config.start_frequency_hz *
                    std::pow(config.end_frequency_hz / config.start_frequency_hz, progress);
                sample = config.amplitude * std::sin(phase);
                phase += two_pi * frequency / static_cast<double>(config.sample_rate_hz);
            } else {
                sample = config.amplitude * std::sin(
                    two_pi * config.start_frequency_hz * static_cast<double>(frame) /
                    static_cast<double>(config.sample_rate_hz));
            }
            samples[frame] = static_cast<float>(sample);
        }
    }
}

struct AuditionSignalStats final {
    double peak{};
    double rms{};
    double dc{};
    bool finite{true};
};

AuditionSignalStats AnalyzeAuditionSignal(const solo_rig::AudioBuffer& buffer,
                                          std::uint32_t sample_count) {
    AuditionSignalStats stats;
    double sum{};
    double sum_squares{};
    for (std::uint32_t channel = 0U; channel < buffer.ChannelCount(); ++channel) {
        const auto* samples = buffer.ChannelData(channel);
        for (std::uint32_t frame = 0U; frame < sample_count; ++frame) {
            const auto sample = static_cast<double>(samples[frame]);
            stats.finite = stats.finite && std::isfinite(sample);
            stats.peak = std::max(stats.peak, std::fabs(sample));
            sum += sample;
            sum_squares += sample * sample;
        }
    }
    const auto count = static_cast<double>(buffer.ChannelCount()) * sample_count;
    if (count > 0.0) {
        stats.dc = sum / count;
        stats.rms = std::sqrt(sum_squares / count);
    }
    return stats;
}

bool EditorHistoryChanged(const EditorHistoryState& previous,
                          const EditorHistoryState& current);

void CommitEditorEdit(SoloRigBackend& backend, EditorHistoryState previous) {
    const auto current = CaptureEditorHistory(backend);
    if (!EditorHistoryChanged(previous, current)) {
        return;
    }
    PushEditorHistory(backend.undo_history, std::move(previous));
    backend.redo_history.clear();
}

void ClearEditorHistory(SoloRigBackend& backend) {
    backend.undo_history.clear();
    backend.redo_history.clear();
    backend.pending_drag_history.reset();
}

bool EditorHistoryLocked(const SoloRigBackend& backend) noexcept {
    return (backend.audio_engine != nullptr && backend.audio_engine->IsOpen()) ||
           (backend.midi_session != nullptr && backend.midi_session->IsOpen());
}

bool EditorLayoutChanged(const solo_rig::RigPreset& previous,
                         const solo_rig::RigPreset& current) noexcept {
    if (previous.nodes.size() != current.nodes.size()) {
        return true;
    }
    for (std::size_t index = 0U; index < previous.nodes.size(); ++index) {
        const auto& old_node = previous.nodes[index];
        const auto& new_node = current.nodes[index];
        if (old_node.instance_id != new_node.instance_id ||
            std::abs(old_node.layout_x - new_node.layout_x) > 0.000001 ||
            std::abs(old_node.layout_y - new_node.layout_y) > 0.000001) {
            return true;
        }
    }
    return false;
}

bool EditorPresetChanged(const solo_rig::RigPreset& previous,
                         const solo_rig::RigPreset& current) {
    return solo_rig::SerializePreset(previous) != solo_rig::SerializePreset(current);
}

bool EditorHistoryChanged(const EditorHistoryState& previous,
                          const EditorHistoryState& current) {
    return EditorPresetChanged(previous.preset, current.preset) ||
           previous.source_path != current.source_path ||
           previous.dirty != current.dirty ||
           previous.resource_status != current.resource_status;
}

bool RestoreEditorHistory(SoloRigBackend& backend, EditorHistoryState state) {
    backend.editor.RestoreDocument(std::move(state.preset), std::move(state.source_path), state.dirty,
                                   std::move(state.file_status), std::move(state.resource_status));
    return backend.midi_session == nullptr || backend.midi_session->SyncFromEditor();
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

constexpr uint32_t kMaximumAsioRouteCount = 64U;
constexpr uint32_t kMaximumAsioProcessingChannels = 32U;

SoloRigStatus OpenAsioRouteMap(
    SoloRigBackend* backend,
    void* native_window,
    const SoloRigAsioChannelRoute* input_routes,
    uint32_t input_route_count,
    const SoloRigAsioChannelRoute* output_routes,
    uint32_t output_route_count) {
    if (!ValidBackend(backend) || backend->audio_engine == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "audio backend is unavailable");
    }
    if (input_routes == nullptr || output_routes == nullptr || input_route_count == 0U ||
        output_route_count == 0U || input_route_count > kMaximumAsioRouteCount ||
        output_route_count > kMaximumAsioRouteCount) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                        "at least one input/output route and bounded route arrays are required");
    }

    const auto& drivers = backend->audio_engine->Drivers();
    solo_rig::StandaloneAudioRouting routing{
        .processing_format = {.sample_rate_hz = 192000.0,
                              .max_block_size = 1U,
                              .channel_count = 0U,
                              .sample_representation = solo_rig::SampleRepresentation::Float32},
    };
    routing.inputs.reserve(input_route_count);
    routing.outputs.reserve(output_route_count);
    std::vector<std::optional<solo_rig::standalone::AsioProbeResult>> probes(drivers.size());
    const auto append_routes = [&](const SoloRigAsioChannelRoute* routes,
                                   uint32_t route_count,
                                   bool input) -> SoloRigStatus {
        for (uint32_t index = 0U; index < route_count; ++index) {
            const auto& route = routes[index];
            if (route.driver_index >= drivers.size()) {
                return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND,
                                "ASIO route references an unknown driver index");
            }
            if (route.processing_channel >= kMaximumAsioProcessingChannels) {
                return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                                "ASIO route processing channel is out of the supported range");
            }
            auto& cached_probe = probes[route.driver_index];
            if (!cached_probe.has_value()) {
                cached_probe.emplace(solo_rig::standalone::AsioHost::ProbeDriver(
                    drivers[route.driver_index], static_cast<HWND>(native_window)));
            }
            const auto& probe = *cached_probe;
            if (!probe.succeeded) {
                return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND,
                                "ASIO probe failed: " + probe.driver_error);
            }
            const auto channel_count = input ? probe.device.input_channel_count
                                             : probe.device.output_channel_count;
            if (route.device_channel >= channel_count) {
                return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                                input ? "ASIO input route channel is out of range"
                                      : "ASIO output route channel is out of range");
            }
            const solo_rig::PhysicalChannelRoute physical_route{
                .device_id = probe.device.device_id,
                .device_channel = route.device_channel,
                .processing_channel = route.processing_channel,
            };
            if (input) {
                routing.inputs.push_back(physical_route);
            } else {
                routing.outputs.push_back(physical_route);
            }
            routing.processing_format.channel_count = std::max(
                routing.processing_format.channel_count, route.processing_channel + 1U);
        }
        return SOLO_RIG_STATUS_OK;
    };

    auto status = append_routes(input_routes, input_route_count, true);
    if (status != SOLO_RIG_STATUS_OK) {
        return status;
    }
    status = append_routes(output_routes, output_route_count, false);
    if (status != SOLO_RIG_STATUS_OK) {
        return status;
    }
    for (std::size_t left = 0U; left < routing.inputs.size(); ++left) {
        for (std::size_t right = left + 1U; right < routing.inputs.size(); ++right) {
            const auto& first = routing.inputs[left];
            const auto& second = routing.inputs[right];
            if ((first.device_id == second.device_id && first.device_channel == second.device_channel) ||
                first.processing_channel == second.processing_channel) {
                return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                                "ASIO input routes reuse a physical or processing channel");
            }
        }
    }
    for (std::size_t left = 0U; left < routing.outputs.size(); ++left) {
        for (std::size_t right = left + 1U; right < routing.outputs.size(); ++right) {
            const auto& first = routing.outputs[left];
            const auto& second = routing.outputs[right];
            if (first.device_id == second.device_id && first.device_channel == second.device_channel) {
                return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                                "ASIO output routes reuse a physical channel");
            }
        }
    }
    if (routing.processing_format.channel_count == 0U) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                        "ASIO routes do not expose a processing channel");
    }

    if (!backend->audio_engine->OpenWithDevices(
            drivers, static_cast<HWND>(native_window), backend->editor.ExportPreset(), std::move(routing))) {
        return SetError(backend, SOLO_RIG_STATUS_INTERNAL_ERROR,
                        backend->audio_engine->ErrorText());
    }
    SyncAudioStatus(*backend);
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

} // namespace

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_create(SoloRigBackend** out_backend) {
    if (out_backend == nullptr) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    *out_backend = nullptr;
    try {
        *out_backend = new SoloRigBackend{};
        (*out_backend)->audio_engine = std::make_unique<solo_rig::standalone::StandaloneAudioEngine>();
        (*out_backend)->midi_session = std::make_unique<solo_rig::standalone::StandaloneMidiControlSession>(
            (*out_backend)->editor);
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
        SyncAudioStatus(*backend);
        SyncMidiStatus(*backend);
        auto snapshot = std::make_unique<SoloRigSnapshot>();
        snapshot->preset = backend->editor.ExportPreset();
        snapshot->scene = backend->editor.Scene();
        const auto& catalog = backend->editor.Catalog();
        snapshot->node_ports.reserve(snapshot->scene.nodes.size());
        snapshot->node_resources.reserve(snapshot->scene.nodes.size());
        for (const auto& node : snapshot->scene.nodes) {
            const auto catalog_entry = std::ranges::find_if(catalog, [&node](const auto& entry) {
                return entry.module_id == node.module_id;
            });
            std::vector<SoloRigSnapshot::NodePort> ports;
            SoloRigSnapshot::NodeResource resource;
            if (catalog_entry != catalog.end()) {
                ports.reserve(catalog_entry->ports.size());
                for (const auto& port : catalog_entry->ports) {
                    ports.push_back({.id = port.id,
                                     .direction = port.direction == solo_rig::PortDirection::Output ? 1U : 0U,
                                     .max_channels = port.max_channels});
                }
                resource.kind = catalog_entry->resource_kind;
                resource.path = catalog_entry->resource_path;
                resource.status = catalog_entry->resource_status;
            }
            if (ports.empty() && node.unresolved) {
                resource.kind = "Module resource";
                resource.path = node.module_id;
                resource.status = node.diagnostic.empty() ? "Unresolved" : node.diagnostic;
            } else if (resource.kind.empty()) {
                resource.kind = "Module resource";
                resource.path = node.module_id;
                resource.status = node.unresolved ? "Unresolved" : "Ready";
            } else if (node.unresolved) {
                resource.status = node.diagnostic.empty() ? "Unresolved" : node.diagnostic;
            }
            snapshot->node_ports.push_back(std::move(ports));
            snapshot->node_resources.push_back(std::move(resource));
        }
        snapshot->node_skin_images_utf8.reserve(snapshot->scene.nodes.size());
        for (const auto& node : snapshot->scene.nodes) {
            snapshot->node_skin_images_utf8.push_back({PathToUtf8(node.skin.knob_image),
                                                       PathToUtf8(node.skin.toggle_image),
                                                       PathToUtf8(node.skin.button_image),
                                                       PathToUtf8(node.skin.footswitch_image)});
        }
        snapshot->parameter_ranges.reserve(snapshot->preset.nodes.size());
        for (const auto& node : snapshot->preset.nodes) {
            std::vector<std::pair<float, float>> ranges;
            ranges.reserve(node.parameter_values.size());
            const auto catalog_entry = std::ranges::find_if(catalog, [&node](const auto& entry) {
                return entry.module_id == node.module_id;
            });
            for (const auto& parameter : node.parameter_values) {
                const auto& parameter_id = parameter.first;
                std::pair<float, float> range{0.0F, 1.0F};
                if (catalog_entry != catalog.end()) {
                    const auto descriptor = std::ranges::find_if(
                        catalog_entry->parameter_descriptors, [&parameter_id](const auto& item) {
                            return item.id == parameter_id;
                        });
                    if (descriptor != catalog_entry->parameter_descriptors.end()) {
                        range = {descriptor->minimum, descriptor->maximum};
                    }
                }
                ranges.push_back(range);
            }
            snapshot->parameter_ranges.push_back(std::move(ranges));
        }
        snapshot->midi_status = backend->midi_session != nullptr
            ? backend->midi_session->StatusText()
            : "MIDI control unavailable";
        if (backend->midi_session != nullptr) {
            const auto& midi_status = backend->midi_session->Status();
            snapshot->midi_open = backend->midi_session->IsOpen();
            snapshot->midi_learn_armed = backend->midi_session->IsLearnArmed();
            snapshot->midi_state = static_cast<std::uint8_t>(midi_status.state);
            snapshot->midi_selected_device_index = midi_status.selected_device_index;
            snapshot->midi_dropped_message_count = midi_status.dropped_messages;
        }
        if (backend->audio_engine != nullptr) {
            snapshot->audio_open = backend->audio_engine->IsOpen();
            snapshot->audio_running = backend->audio_engine->IsRunning();
            snapshot->audio_reset_requested = backend->audio_engine->ResetRequested();
            snapshot->audio_dropout_reported = backend->audio_engine->DropoutReported();
            snapshot->audio_sample_rate_changed_to = backend->audio_engine->SampleRateChangedTo();
            snapshot->tuner_enabled = backend->audio_engine->TunerEnabled();
            if (const auto tuning = backend->audio_engine->AnalyzeTuner(); tuning.has_value()) {
                snapshot->tuner_signal_present = true;
                snapshot->tuner_midi_note = tuning->midi_note;
                snapshot->tuner_frequency_hz = tuning->frequency_hz;
                snapshot->tuner_cents_from_nearest_note = tuning->cents_from_nearest_note;
                snapshot->tuner_confidence = tuning->confidence;
            }
        }
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
    out_info->schema_version = 4U;
    out_info->dark_theme = snapshot->scene.theme.id.find("dark") != std::string::npos ? 1U : 0U;
    out_info->preset_id_utf8 = snapshot->preset.preset_id.c_str();
    out_info->preset_name_utf8 = snapshot->preset.name.c_str();
    out_info->source_path_utf8 = snapshot->scene.source_path.c_str();
    out_info->audio_status_utf8 = snapshot->scene.audio_status.c_str();
    out_info->file_status_utf8 = snapshot->scene.file_status.c_str();
    out_info->midi_status_utf8 = snapshot->midi_status.c_str();
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

SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_audio_state(
    const SoloRigSnapshot* snapshot,
    SoloRigAudioStateView* out_state) {
    if (snapshot == nullptr || out_state == nullptr) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    out_state->is_open = snapshot->audio_open ? 1U : 0U;
    out_state->is_running = snapshot->audio_running ? 1U : 0U;
    out_state->reset_requested = snapshot->audio_reset_requested ? 1U : 0U;
    out_state->dropout_reported = snapshot->audio_dropout_reported ? 1U : 0U;
    out_state->sample_rate_changed_to = snapshot->audio_sample_rate_changed_to;
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_tuner_state(
    const SoloRigSnapshot* snapshot,
    SoloRigTunerStateView* out_state) {
    if (snapshot == nullptr || out_state == nullptr) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    out_state->enabled = snapshot->tuner_enabled ? 1U : 0U;
    out_state->signal_present = snapshot->tuner_signal_present ? 1U : 0U;
    out_state->reserved0 = 0U;
    out_state->reserved1 = 0U;
    out_state->midi_note = snapshot->tuner_midi_note;
    out_state->frequency_hz = snapshot->tuner_frequency_hz;
    out_state->cents_from_nearest_note = snapshot->tuner_cents_from_nearest_note;
    out_state->confidence = snapshot->tuner_confidence;
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_midi_state(
    const SoloRigSnapshot* snapshot,
    SoloRigMidiStateView* out_state) {
    if (snapshot == nullptr || out_state == nullptr) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    out_state->is_open = snapshot->midi_open ? 1U : 0U;
    out_state->learn_armed = snapshot->midi_learn_armed ? 1U : 0U;
    out_state->state = snapshot->midi_state;
    out_state->reserved = 0U;
    out_state->selected_device_index = snapshot->midi_selected_device_index;
    out_state->dropped_message_count = snapshot->midi_dropped_message_count;
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
    out_node->bypassed = node->bypassed ? 1U : 0U;
    out_node->skin_id_utf8 = node->skin.id.c_str();
    out_node->knob_skin_id_utf8 = node->skin.knob.c_str();
    out_node->toggle_skin_id_utf8 = node->skin.toggle.c_str();
    out_node->button_skin_id_utf8 = node->skin.button.c_str();
    out_node->footswitch_skin_id_utf8 = node->skin.footswitch.c_str();
    const auto& images = snapshot->node_skin_images_utf8[index];
    out_node->knob_image_path_utf8 = images[0].c_str();
    out_node->toggle_image_path_utf8 = images[1].c_str();
    out_node->button_image_path_utf8 = images[2].c_str();
    out_node->footswitch_image_path_utf8 = images[3].c_str();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_node_port_count(
    const SoloRigSnapshot* snapshot,
    uint32_t node_index,
    uint32_t* out_count) {
    if (snapshot == nullptr || out_count == nullptr || node_index >= snapshot->node_ports.size()) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    *out_count = static_cast<uint32_t>(snapshot->node_ports[node_index].size());
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_node_port(
    const SoloRigSnapshot* snapshot,
    uint32_t node_index,
    uint32_t port_index,
    SoloRigNodePortView* out_port) {
    if (snapshot == nullptr || out_port == nullptr || node_index >= snapshot->node_ports.size() ||
        port_index >= snapshot->node_ports[node_index].size()) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    const auto& port = snapshot->node_ports[node_index][port_index];
    out_port->port_id_utf8 = port.id.c_str();
    out_port->direction = port.direction;
    out_port->max_channels = port.max_channels;
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_node_resource(
    const SoloRigSnapshot* snapshot,
    uint32_t node_index,
    SoloRigNodeResourceView* out_resource) {
    if (snapshot == nullptr || out_resource == nullptr || node_index >= snapshot->node_resources.size()) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    const auto& resource = snapshot->node_resources[node_index];
    out_resource->kind_utf8 = resource.kind.c_str();
    out_resource->path_utf8 = resource.path.c_str();
    out_resource->status_utf8 = resource.status.c_str();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_node_skin_layout(
    const SoloRigSnapshot* snapshot,
    uint32_t node_index,
    SoloRigNodeSkinLayoutView* out_layout) {
    const auto* node = FindSceneNode(snapshot, node_index);
    if (node == nullptr || out_layout == nullptr) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    out_layout->width = node->skin.width;
    out_layout->height = node->skin.height;
    out_layout->background_color_utf8 = node->skin.background_color.c_str();
    out_layout->label_color_utf8 = node->skin.label_color.c_str();
    out_layout->has_footswitch_position = node->skin.footswitch_position.has_value() ? 1U : 0U;
    out_layout->footswitch_x = node->skin.footswitch_position.has_value() ? node->skin.footswitch_position->x : 0.0;
    out_layout->footswitch_y = node->skin.footswitch_position.has_value() ? node->skin.footswitch_position->y : 0.0;
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_node_skin_parameter_layout_count(
    const SoloRigSnapshot* snapshot,
    uint32_t node_index,
    uint32_t* out_count) {
    const auto* node = FindSceneNode(snapshot, node_index);
    if (node == nullptr || out_count == nullptr) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    *out_count = static_cast<uint32_t>(node->skin.parameter_placements.size());
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_snapshot_node_skin_parameter_layout(
    const SoloRigSnapshot* snapshot,
    uint32_t node_index,
    uint32_t layout_index,
    SoloRigNodeSkinParameterLayoutView* out_layout) {
    const auto* node = FindSceneNode(snapshot, node_index);
    if (node == nullptr || out_layout == nullptr || layout_index >= node->skin.parameter_placements.size()) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    const auto& placement = node->skin.parameter_placements[layout_index];
    out_layout->parameter_id_utf8 = placement.parameter_id.c_str();
    out_layout->x = placement.x;
    out_layout->y = placement.y;
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
    out_parameter->minimum = 0.0F;
    out_parameter->maximum = 1.0F;
    if (node_index < snapshot->parameter_ranges.size() &&
        parameter_index < snapshot->parameter_ranges[node_index].size()) {
        out_parameter->minimum = snapshot->parameter_ranges[node_index][parameter_index].first;
        out_parameter->maximum = snapshot->parameter_ranges[node_index][parameter_index].second;
    }
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
    return solorig_backend_set_parameter_utf8_with_dirty(backend, instance_id_utf8, parameter_id_utf8, value, 1U);
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_set_parameter_utf8_with_dirty(
    SoloRigBackend* backend,
    const char* instance_id_utf8,
    const char* parameter_id_utf8,
    float value,
    uint8_t mark_dirty) {
    if (!ValidBackend(backend) || !ValidText(instance_id_utf8) || !ValidText(parameter_id_utf8) ||
        !std::isfinite(value) || mark_dirty > 1U) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "valid parameter identifiers and finite value are required");
    }
    const auto previous = mark_dirty != 0U
        ? std::optional<EditorHistoryState>(CaptureEditorHistory(*backend))
        : std::nullopt;
    if (!backend->editor.SetNodeParameter(instance_id_utf8, parameter_id_utf8, value, mark_dirty != 0U)) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "parameter target was not found or value was rejected");
    }
    if (previous.has_value()) {
        CommitEditorEdit(*backend, std::move(*previous));
    }
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_set_node_bypassed_utf8(
    SoloRigBackend* backend,
    const char* instance_id_utf8,
    uint8_t bypassed) {
    if (!ValidBackend(backend) || !ValidText(instance_id_utf8) || bypassed > 1U) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "a valid node identifier and bypass value are required");
    }
    auto previous = CaptureEditorHistory(*backend);
    if (!backend->editor.SetNodeBypassed(instance_id_utf8, bypassed != 0U)) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "bypass node was not found");
    }
    if (backend->audio_engine != nullptr && backend->audio_engine->IsOpen() &&
        !backend->audio_engine->SetNodeBypassed(instance_id_utf8, bypassed != 0U)) {
        const auto audio_error = backend->audio_engine->ErrorText();
        static_cast<void>(RestoreEditorHistory(*backend, std::move(previous)));
        return SetError(backend, SOLO_RIG_STATUS_INTERNAL_ERROR,
                        audio_error.empty() ? "audio graph rejected bypass change" : audio_error);
    }
    CommitEditorEdit(*backend, std::move(previous));
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_set_nodes_bypassed(
    SoloRigBackend* backend,
    const SoloRigNodeBatchEntry* nodes,
    uint32_t node_count,
    uint8_t bypassed) {
    constexpr uint32_t kMaximumBatchNodeCount = 128U;
    if (!ValidBackend(backend) || nodes == nullptr || node_count == 0U ||
        node_count > kMaximumBatchNodeCount || bypassed > 1U) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                        "a bounded node batch and bypass value are required");
    }
    std::vector<std::string> instance_ids;
    instance_ids.reserve(node_count);
    std::set<std::string, std::less<>> unique_ids;
    for (uint32_t index = 0U; index < node_count; ++index) {
        if (!ValidText(nodes[index].instance_id_utf8)) {
            return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                            "every batch node requires a valid instance ID");
        }
        std::string instance_id(nodes[index].instance_id_utf8);
        if (!unique_ids.insert(instance_id).second) {
            return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                            "batch node instance IDs must be unique");
        }
        instance_ids.push_back(std::move(instance_id));
    }

    auto previous = CaptureEditorHistory(*backend);
    std::map<std::string, bool, std::less<>> original_states;
    for (const auto& node : previous.preset.nodes) {
        if (unique_ids.contains(node.instance_id)) {
            original_states.emplace(node.instance_id, node.bypassed);
        }
    }
    if (original_states.size() != unique_ids.size() ||
        !backend->editor.SetNodesBypassed(instance_ids, bypassed != 0U)) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND,
                        "one or more batch nodes were not found");
    }

    if (backend->audio_engine != nullptr && backend->audio_engine->IsOpen()) {
        std::vector<std::string> applied;
        applied.reserve(instance_ids.size());
        for (const auto& instance_id : instance_ids) {
            if (!backend->audio_engine->SetNodeBypassed(instance_id, bypassed != 0U)) {
                const auto audio_error = backend->audio_engine->ErrorText();
                for (const auto& applied_id : applied) {
                    static_cast<void>(backend->audio_engine->SetNodeBypassed(applied_id, original_states[applied_id]));
                }
                static_cast<void>(RestoreEditorHistory(*backend, std::move(previous)));
                return SetError(backend, SOLO_RIG_STATUS_INTERNAL_ERROR,
                                audio_error.empty() ? "audio graph rejected batch bypass change" : audio_error);
            }
            applied.push_back(instance_id);
        }
    }
    CommitEditorEdit(*backend, std::move(previous));
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

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_restore_document_state(
    SoloRigBackend* backend,
    uint8_t dirty) {
    if (!ValidBackend(backend) || dirty > 1U) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "document dirty state must be 0 or 1");
    }
    const auto preset = backend->editor.ExportPreset();
    const auto source = backend->editor.SourcePath();
    const auto status = std::string(backend->editor.FileStatus());
    backend->editor.RestoreDocument(preset, source, dirty != 0U, status);
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API uint8_t solorig_backend_can_undo(const SoloRigBackend* backend) {
    if (backend == nullptr || EditorHistoryLocked(*backend)) {
        return 0U;
    }
    return backend->undo_history.empty() ? 0U : 1U;
}

SOLO_RIG_BACKEND_API uint8_t solorig_backend_can_redo(const SoloRigBackend* backend) {
    if (backend == nullptr || EditorHistoryLocked(*backend)) {
        return 0U;
    }
    return backend->redo_history.empty() ? 0U : 1U;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_undo(SoloRigBackend* backend) {
    if (!ValidBackend(backend)) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    if (EditorHistoryLocked(*backend)) {
        return SetError(backend, SOLO_RIG_STATUS_UNSUPPORTED,
                        "close ASIO and MIDI before undoing document edits");
    }
    if (backend->undo_history.empty()) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "no document edit is available to undo");
    }
    const auto previous = backend->undo_history.back();
    backend->undo_history.pop_back();
    const auto current = CaptureEditorHistory(*backend);
    if (!RestoreEditorHistory(*backend, previous)) {
        PushEditorHistory(backend->undo_history, previous);
        return SetError(backend, SOLO_RIG_STATUS_INTERNAL_ERROR,
                        "document undo could not synchronize the MIDI controller");
    }
    PushEditorHistory(backend->redo_history, current);
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_redo(SoloRigBackend* backend) {
    if (!ValidBackend(backend)) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    if (EditorHistoryLocked(*backend)) {
        return SetError(backend, SOLO_RIG_STATUS_UNSUPPORTED,
                        "close ASIO and MIDI before redoing document edits");
    }
    if (backend->redo_history.empty()) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "no document edit is available to redo");
    }
    const auto next = backend->redo_history.back();
    backend->redo_history.pop_back();
    const auto current = CaptureEditorHistory(*backend);
    if (!RestoreEditorHistory(*backend, next)) {
        PushEditorHistory(backend->redo_history, next);
        return SetError(backend, SOLO_RIG_STATUS_INTERNAL_ERROR,
                        "document redo could not synchronize the MIDI controller");
    }
    PushEditorHistory(backend->undo_history, current);
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_clear_history(SoloRigBackend* backend) {
    if (!ValidBackend(backend)) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    ClearEditorHistory(*backend);
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_set_theme_dark(
    SoloRigBackend* backend,
    uint8_t dark) {
    return solorig_backend_set_theme_dark_with_dirty(backend, dark, 1U);
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_set_theme_dark_with_dirty(
    SoloRigBackend* backend,
    uint8_t dark,
    uint8_t mark_dirty) {
    if (!ValidBackend(backend) || dark > 1U || mark_dirty > 1U) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "theme mode must be 0 or 1");
    }
    const auto previous = mark_dirty != 0U
        ? std::optional<EditorHistoryState>(CaptureEditorHistory(*backend))
        : std::nullopt;
    backend->editor.SetThemeMode(dark != 0U, mark_dirty != 0U);
    if (previous.has_value()) {
        CommitEditorEdit(*backend, std::move(*previous));
    }
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_load_theme_utf8(
    SoloRigBackend* backend,
    const char* path_utf8) {
    return solorig_backend_load_theme_utf8_with_dirty(backend, path_utf8, 1U);
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_load_theme_utf8_with_dirty(
    SoloRigBackend* backend,
    const char* path_utf8,
    uint8_t mark_dirty) {
    if (!ValidBackend(backend) || !ValidText(path_utf8) || mark_dirty > 1U) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "a theme path is required");
    }
    const auto previous = mark_dirty != 0U
        ? std::optional<EditorHistoryState>(CaptureEditorHistory(*backend))
        : std::nullopt;
    if (!backend->editor.LoadThemeFile(PathFromUtf8(path_utf8), mark_dirty != 0U)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_THEME, "theme file could not be loaded");
    }
    if (previous.has_value()) {
        CommitEditorEdit(*backend, std::move(*previous));
    }
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_theme_info(
    SoloRigBackend* backend,
    SoloRigThemeInfoView* out_theme) {
    if (!ValidBackend(backend) || out_theme == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                        "backend and theme output are required");
    }
    const auto& theme = backend->editor.Scene().theme;
    out_theme->id_utf8 = theme.id.c_str();
    out_theme->display_name_utf8 = theme.display_name.c_str();
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API const char* solorig_backend_theme_color_utf8(
    SoloRigBackend* backend,
    const char* color_key_utf8) {
    if (!ValidBackend(backend) || !ValidText(color_key_utf8)) {
        return nullptr;
    }
    const auto& colors = backend->editor.Scene().theme.colors;
    const auto iterator = colors.find(color_key_utf8);
    return iterator == colors.end() ? nullptr : iterator->second.c_str();
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

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_clear_selection(
    SoloRigBackend* backend) {
    if (!ValidBackend(backend)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "a backend is required");
    }
    backend->editor.ClearSelection();
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
    backend->pending_drag_history = CaptureEditorHistory(*backend);
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
        if (backend->pending_drag_history.has_value()) {
            const auto current = backend->editor.ExportPreset();
            if (EditorLayoutChanged(backend->pending_drag_history->preset, current)) {
                CommitEditorEdit(*backend, std::move(*backend->pending_drag_history));
            }
            backend->pending_drag_history.reset();
        }
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
    const auto result = backend->editor.LoadFrom(PathFromUtf8(path_utf8));
    const auto mapped = MapFileResult(backend, result);
    if (mapped != SOLO_RIG_STATUS_OK) {
        return mapped;
    }
    if (backend->midi_session != nullptr && !backend->midi_session->SyncFromEditor()) {
        return SetError(backend, SOLO_RIG_STATUS_INTERNAL_ERROR, "MIDI bindings could not be synchronized");
    }
    ClearEditorHistory(*backend);
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_new_preset(SoloRigBackend* backend) {
    if (!ValidBackend(backend)) {
        return SOLO_RIG_STATUS_INVALID_ARGUMENT;
    }
    backend->editor.NewPreset();
    if (backend->midi_session != nullptr && !backend->midi_session->SyncFromEditor()) {
        return SetError(backend, SOLO_RIG_STATUS_INTERNAL_ERROR, "MIDI bindings could not be synchronized");
    }
    ClearEditorHistory(*backend);
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
    const auto previous = CaptureEditorHistory(*backend);
    if (!backend->editor.AddNode(module_id_utf8, instance_id_utf8, layout_x, layout_y)) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "module node could not be added");
    }
    CommitEditorEdit(*backend, std::move(previous));
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_add_node_to_chain_utf8(
    SoloRigBackend* backend, const char* module_id_utf8, const char* instance_id_utf8,
    double layout_x, double layout_y) {
    if (!ValidBackend(backend) || !ValidText(module_id_utf8) || !ValidText(instance_id_utf8) ||
        !std::isfinite(layout_x) || !std::isfinite(layout_y)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "module, instance and finite layout are required");
    }
    const auto previous = CaptureEditorHistory(*backend);
    const auto before = backend->editor.ExportPreset();
    std::optional<std::string> terminal_node;
    for (const auto& node : before.nodes) {
        const auto has_successor = std::ranges::any_of(before.connections, [&node](const auto& connection) {
            return connection.source_instance_id == node.instance_id;
        });
        if (!has_successor) {
            terminal_node = node.instance_id;
        }
    }
    if (!backend->editor.AddNode(module_id_utf8, instance_id_utf8, layout_x, layout_y)) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "module node could not be added");
    }
    if (terminal_node.has_value() && *terminal_node != instance_id_utf8 &&
        !backend->editor.ConnectNodes(*terminal_node, instance_id_utf8)) {
        static_cast<void>(RestoreEditorHistory(*backend, previous));
        return SetError(backend, SOLO_RIG_STATUS_INVALID_PRESET,
                        "new module could not be appended to the effect chain");
    }
    CommitEditorEdit(*backend, std::move(previous));
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_delete_node_utf8(
    SoloRigBackend* backend, const char* instance_id_utf8) {
    if (!ValidBackend(backend) || !ValidText(instance_id_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "a node instance ID is required");
    }
    const auto previous = CaptureEditorHistory(*backend);
    if (!backend->editor.DeleteNode(instance_id_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "node could not be deleted");
    }
    CommitEditorEdit(*backend, std::move(previous));
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_duplicate_node_utf8(
    SoloRigBackend* backend, const char* source_instance_id_utf8, const char* new_instance_id_utf8) {
    if (!ValidBackend(backend) || !ValidText(source_instance_id_utf8) || !ValidText(new_instance_id_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "source and destination node IDs are required");
    }
    const auto previous = CaptureEditorHistory(*backend);
    if (!backend->editor.DuplicateNode(source_instance_id_utf8, new_instance_id_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "node could not be duplicated");
    }
    CommitEditorEdit(*backend, std::move(previous));
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_connect_utf8(
    SoloRigBackend* backend, const char* source_instance_id_utf8, const char* destination_instance_id_utf8) {
    if (!ValidBackend(backend) || !ValidText(source_instance_id_utf8) || !ValidText(destination_instance_id_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "source and destination node IDs are required");
    }
    const auto previous = CaptureEditorHistory(*backend);
    if (!backend->editor.ConnectNodes(source_instance_id_utf8, destination_instance_id_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_PRESET, "connection was rejected by topology validation");
    }
    CommitEditorEdit(*backend, std::move(previous));
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_disconnect_utf8(
    SoloRigBackend* backend, const char* source_instance_id_utf8, const char* destination_instance_id_utf8) {
    if (!ValidBackend(backend) || !ValidText(source_instance_id_utf8) || !ValidText(destination_instance_id_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "source and destination node IDs are required");
    }
    const auto previous = CaptureEditorHistory(*backend);
    if (!backend->editor.DisconnectNodes(source_instance_id_utf8, destination_instance_id_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "connection was not found");
    }
    CommitEditorEdit(*backend, std::move(previous));
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_import_resource_utf8(
    SoloRigBackend* backend, const char* path_utf8) {
    if (!ValidBackend(backend) || !ValidText(path_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "an explicit resource path is required");
    }
    const auto previous = CaptureEditorHistory(*backend);
    const auto result = backend->editor.ImportResource(PathFromUtf8(path_utf8));
    if (!result.success) {
        if (result.error == solo_rig::EditorFileError::CannotRead) {
            return SetError(backend, SOLO_RIG_STATUS_FILE_ERROR, result.message);
        }
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, result.message);
    }
    CommitEditorEdit(*backend, std::move(previous));
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_validate_module_package_utf8(
    SoloRigBackend* backend, const char* directory_path_utf8) {
    if (!ValidBackend(backend) || !ValidText(directory_path_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "a module package directory is required");
    }
    const auto directory = PathFromUtf8(directory_path_utf8);
    if (!std::filesystem::is_directory(directory)) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "module package directory was not found");
    }
    const auto result = solo_rig::LoadModulePackage(directory);
    if (!result.package.has_value()) {
        const auto message = result.errors.empty()
            ? std::string("module package validation failed")
            : std::string(ModulePackageErrorText(result.errors.front()));
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, message);
    }
    constexpr double sample_rate_hz = 48000.0;
    constexpr std::uint32_t block_size = 256U;
    if (const auto* circuit = std::get_if<solo_rig::CircuitNetlist>(&result.package->logic)) {
        auto compiled = solo_rig::CompileMnaCircuit(*circuit);
        if (!compiled.circuit.has_value()) {
            const auto message = compiled.error.has_value()
                ? std::string(MnaCompileErrorText(*compiled.error))
                : std::string("circuit compilation failed");
            return SetError(backend, SOLO_RIG_STATUS_UNSUPPORTED, message);
        }
        if (!compiled.circuit->Prepare(sample_rate_hz)) {
            return SetError(backend, SOLO_RIG_STATUS_UNSUPPORTED,
                            "circuit runtime rejected the validation sample rate");
        }
        std::vector<double> inputs(compiled.circuit->InputCount(), 0.0);
        const auto solved = compiled.circuit->Solve(inputs);
        if (!solved.Succeeded()) {
            return SetError(backend, SOLO_RIG_STATUS_UNSUPPORTED,
                            std::string(MnaSolveErrorText(solved.error)));
        }
        for (std::size_t output = 0U; output < compiled.circuit->OutputCount(); ++output) {
            const auto value = compiled.circuit->OutputValue(output);
            if (!value.has_value() || !std::isfinite(*value)) {
                return SetError(backend, SOLO_RIG_STATUS_INTERNAL_ERROR,
                                "circuit validation produced a non-finite output");
            }
        }
    } else {
        auto runtime = solo_rig::CreateModuleRuntime(*result.package);
        if (const auto* error = std::get_if<solo_rig::ModuleRuntimeError>(&runtime); error != nullptr) {
            return SetError(backend, SOLO_RIG_STATUS_UNSUPPORTED, std::string(ModuleRuntimeErrorText(*error)));
        }
        auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
        const solo_rig::AudioFormat format{
            .sample_rate_hz = sample_rate_hz,
            .max_block_size = block_size,
            .channel_count = 1U,
        };
        if (!processor->Prepare(format)) {
            return SetError(backend, SOLO_RIG_STATUS_UNSUPPORTED,
                            "module runtime rejected the validation format");
        }
    }
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_audition_module_package_utf8(
    SoloRigBackend* backend, const char* directory_path_utf8, SoloRigModuleAuditionView* out_audition) {
    if (!ValidBackend(backend) || !ValidText(directory_path_utf8) || out_audition == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                        "a module package directory and audition output are required");
    }
    const auto directory = PathFromUtf8(directory_path_utf8);
    const auto package = solo_rig::LoadModulePackage(directory);
    if (!package.package.has_value()) {
        const auto message = package.errors.empty()
            ? std::string("module package validation failed")
            : std::string(ModulePackageErrorText(package.errors.front()));
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, message);
    }
    auto runtime = solo_rig::CreateModuleRuntime(*package.package);
    if (const auto* error = std::get_if<solo_rig::ModuleRuntimeError>(&runtime); error != nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_UNSUPPORTED, std::string(ModuleRuntimeErrorText(*error)));
    }
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    constexpr std::uint32_t sample_rate = 48000U;
    constexpr std::uint32_t block_size = 256U;
    const auto input_channels = std::ranges::find_if(package.package->descriptor.ports, [](const auto& port) {
        return port.direction == solo_rig::PortDirection::Input;
    });
    const auto output_channels = std::ranges::find_if(package.package->descriptor.ports, [](const auto& port) {
        return port.direction == solo_rig::PortDirection::Output;
    });
    if (input_channels == package.package->descriptor.ports.end() ||
        output_channels == package.package->descriptor.ports.end()) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "module must expose input and output ports");
    }
    const auto channel_count = std::min(input_channels->max_channels, output_channels->max_channels);
    if (channel_count == 0U || channel_count > 2U) {
        return SetError(backend, SOLO_RIG_STATUS_UNSUPPORTED, "offline audition supports one or two channels");
    }
    const solo_rig::AudioFormat format{
        .sample_rate_hz = static_cast<double>(sample_rate),
        .max_block_size = block_size,
        .channel_count = channel_count,
    };
    if (!processor->Prepare(format)) {
        return SetError(backend, SOLO_RIG_STATUS_UNSUPPORTED, "module runtime rejected the audition format");
    }
    solo_rig::AudioBuffer input(channel_count, block_size);
    solo_rig::AudioBuffer output(channel_count, block_size);
    constexpr double two_pi = 6.28318530717958647692;
    for (std::uint32_t channel = 0U; channel < channel_count; ++channel) {
        auto* samples = input.ChannelData(channel);
        for (std::uint32_t frame = 0U; frame < block_size; ++frame) {
            samples[frame] = static_cast<float>(0.1 * std::sin(two_pi * 220.0 * frame / sample_rate));
        }
        samples[0] += 0.25F;
    }
    const auto started = std::chrono::steady_clock::now();
    processor->Process(input, output, block_size);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    double peak = 0.0;
    double sum_squares = 0.0;
    bool finite = true;
    for (std::uint32_t channel = 0U; channel < channel_count; ++channel) {
        const auto* samples = output.ChannelData(channel);
        for (std::uint32_t frame = 0U; frame < block_size; ++frame) {
            const auto sample = static_cast<double>(samples[frame]);
            finite = finite && std::isfinite(sample);
            peak = std::max(peak, std::fabs(sample));
            sum_squares += sample * sample;
        }
    }
    const auto audio_seconds = static_cast<double>(block_size) / sample_rate;
    const auto elapsed_seconds = std::chrono::duration<double>(elapsed).count();
    *out_audition = SoloRigModuleAuditionView{
        .sample_rate_hz = sample_rate,
        .block_size = block_size,
        .channel_count = channel_count,
        .declared_latency_samples = processor->DeclaredLatencySamples(),
        .output_peak = peak,
        .output_rms = std::sqrt(sum_squares / static_cast<double>(block_size * channel_count)),
        .processing_cpu_percent = audio_seconds > 0.0 ? elapsed_seconds / audio_seconds * 100.0 : 0.0,
        .output_finite = static_cast<std::uint8_t>(finite ? 1U : 0U),
    };
    if (!finite) {
        return SetError(backend, SOLO_RIG_STATUS_INTERNAL_ERROR, "offline audition produced non-finite output");
    }
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

static SoloRigStatus AuditionModulePackageV3Impl(
    SoloRigBackend* backend,
    const char* directory_path_utf8,
    const SoloRigModuleAuditionConfigV2* config,
    float* input_waveform,
    float* output_waveform,
    uint32_t waveform_capacity,
    const char* parameter_id_utf8,
    double parameter_value,
    bool use_parameter_override,
    SoloRigModuleAuditionAnalysisV3* out_analysis) {
    if (!ValidBackend(backend) || !ValidText(directory_path_utf8) || config == nullptr ||
        input_waveform == nullptr || output_waveform == nullptr || out_analysis == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                        "an audition directory, config, waveform buffers, and analysis output are required");
    }
    if (use_parameter_override && (!ValidText(parameter_id_utf8) || !std::isfinite(parameter_value))) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                        "a finite audition parameter override is required");
    }
    if (config->sample_rate_hz < 8000U || config->sample_rate_hz > 384000U ||
        config->block_size < 16U || config->block_size > 8192U ||
        config->channel_count == 0U || config->channel_count > 8U ||
        config->stimulus_kind > 3U || waveform_capacity < config->block_size ||
        !std::isfinite(config->amplitude) || config->amplitude < 0.0 || config->amplitude > 10.0 ||
        ((config->stimulus_kind == 0U || config->stimulus_kind == 2U) &&
         (!std::isfinite(config->start_frequency_hz) || !std::isfinite(config->end_frequency_hz) ||
          config->start_frequency_hz <= 0.0 || config->end_frequency_hz <= 0.0 ||
          config->start_frequency_hz >= static_cast<double>(config->sample_rate_hz) * 0.5 ||
          config->end_frequency_hz >= static_cast<double>(config->sample_rate_hz) * 0.5))) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "offline audition config is invalid");
    }
    if (config->stimulus_kind == 3U) {
        for (std::uint32_t frame = 0U; frame < config->block_size; ++frame) {
            if (!std::isfinite(input_waveform[frame])) {
                return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                                "caller audition waveform contains a non-finite sample");
            }
        }
    }
    const auto package = solo_rig::LoadModulePackage(PathFromUtf8(directory_path_utf8));
    if (!package.package.has_value()) {
        const auto message = package.errors.empty()
            ? std::string("module package validation failed")
            : std::string(ModulePackageErrorText(package.errors.front()));
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, message);
    }
    const auto requested_input = config->input_port_utf8 == nullptr
        ? std::string_view{} : std::string_view(config->input_port_utf8);
    const auto requested_output = config->output_port_utf8 == nullptr
        ? std::string_view{} : std::string_view(config->output_port_utf8);
    solo_rig::AudioBuffer selected_input(config->channel_count, config->block_size);
    solo_rig::AudioBuffer selected_output(config->channel_count, config->block_size);
    FillAuditionStimulus(selected_input, *config, input_waveform);
    std::uint32_t declared_latency{};
    solo_rig::NamedAudioPortDiagnostics solver_diagnostics{};
    const auto started = std::chrono::steady_clock::now();

    if (const auto* circuit = std::get_if<solo_rig::CircuitNetlist>(&package.package->logic)) {
        auto runtime = solo_rig::CreateNamedMnaPortProcessor(*package.package);
        if (const auto* error = std::get_if<solo_rig::ModuleRuntimeError>(&runtime); error != nullptr) {
            return SetError(backend, SOLO_RIG_STATUS_UNSUPPORTED, std::string(ModuleRuntimeErrorText(*error)));
        }
        auto processor = std::move(std::get<std::unique_ptr<solo_rig::NamedAudioPortProcessor>>(runtime));
        const auto input_contracts = processor->InputPorts();
        const auto output_contracts = processor->OutputPorts();
        const auto find_port = [](const auto& contracts, std::string_view requested) -> std::optional<std::size_t> {
            if (contracts.count == 0U) return std::nullopt;
            if (requested.empty()) return 0U;
            for (std::size_t index = 0U; index < contracts.count; ++index) {
                if (contracts.ports[index].name == requested) return index;
            }
            return std::nullopt;
        };
        const auto selected_input_index = find_port(input_contracts, requested_input);
        const auto selected_output_index = find_port(output_contracts, requested_output);
        if (!selected_input_index.has_value() || !selected_output_index.has_value()) {
            return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "selected audition port was not found");
        }
        const solo_rig::AudioFormat format{
            .sample_rate_hz = static_cast<double>(config->sample_rate_hz),
            .max_block_size = config->block_size,
            .channel_count = config->channel_count,
        };
        if (!processor->Prepare(format, circuit->solver.oversampling)) {
            return SetError(backend, SOLO_RIG_STATUS_UNSUPPORTED,
                            "named circuit runtime rejected the audition format");
        }
        if (use_parameter_override &&
            !processor->SetParameter(parameter_id_utf8, static_cast<float>(parameter_value))) {
            return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                            "audition parameter override is invalid or unsupported");
        }
        std::vector<solo_rig::AudioBuffer> input_buffers;
        std::vector<solo_rig::AudioBuffer> output_buffers;
        input_buffers.reserve(input_contracts.count);
        output_buffers.reserve(output_contracts.count);
        for (std::size_t index = 0U; index < input_contracts.count; ++index) {
            input_buffers.emplace_back(config->channel_count, config->block_size);
        }
        for (std::size_t index = 0U; index < output_contracts.count; ++index) {
            output_buffers.emplace_back(config->channel_count, config->block_size);
        }
        if (!input_buffers[*selected_input_index].CopyFrom(selected_input, config->block_size)) {
            return SetError(backend, SOLO_RIG_STATUS_INTERNAL_ERROR,
                            "named circuit audition input copy failed");
        }
        std::vector<solo_rig::NamedAudioPortInput> inputs;
        std::vector<solo_rig::NamedAudioPortOutput> outputs;
        inputs.reserve(input_contracts.count);
        outputs.reserve(output_contracts.count);
        for (std::size_t index = 0U; index < input_contracts.count; ++index) {
            inputs.push_back({input_contracts.ports[index].name,
                              input_contracts.ports[index].domain, &input_buffers[index]});
        }
        for (std::size_t index = 0U; index < output_contracts.count; ++index) {
            outputs.push_back({output_contracts.ports[index].name,
                               output_contracts.ports[index].domain, &output_buffers[index]});
        }
        if (!processor->Process(inputs, outputs, config->block_size) ||
            !selected_output.CopyFrom(output_buffers[*selected_output_index], config->block_size)) {
            return SetError(backend, SOLO_RIG_STATUS_INTERNAL_ERROR, "named circuit audition processing failed");
        }
        declared_latency = processor->DeclaredLatencySamples();
        solver_diagnostics = processor->Diagnostics();
    } else {
        const auto first_port = [&package](solo_rig::PortDirection direction) -> std::string_view {
            const auto port = std::ranges::find_if(package.package->descriptor.ports,
                [direction](const auto& candidate) { return candidate.direction == direction; });
            return port == package.package->descriptor.ports.end() ? std::string_view{} : port->id;
        };
        const auto input_name = first_port(solo_rig::PortDirection::Input);
        const auto output_name = first_port(solo_rig::PortDirection::Output);
        if (input_name.empty() || output_name.empty() ||
            (!requested_input.empty() && requested_input != input_name) ||
            (!requested_output.empty() && requested_output != output_name)) {
            return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "selected audition port was not found");
        }
        auto runtime = solo_rig::CreateModuleRuntime(*package.package);
        if (const auto* error = std::get_if<solo_rig::ModuleRuntimeError>(&runtime); error != nullptr) {
            return SetError(backend, SOLO_RIG_STATUS_UNSUPPORTED, std::string(ModuleRuntimeErrorText(*error)));
        }
        auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
        const solo_rig::AudioFormat format{
            .sample_rate_hz = static_cast<double>(config->sample_rate_hz),
            .max_block_size = config->block_size,
            .channel_count = config->channel_count,
        };
        if (!processor->Prepare(format)) {
            return SetError(backend, SOLO_RIG_STATUS_UNSUPPORTED,
                            "module runtime rejected the audition format");
        }
        if (use_parameter_override &&
            !processor->SetParameter(parameter_id_utf8, static_cast<float>(parameter_value))) {
            return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                            "audition parameter override is invalid or unsupported");
        }
        processor->Process(selected_input, selected_output, config->block_size);
        declared_latency = processor->DeclaredLatencySamples();
    }

    const auto elapsed = std::chrono::steady_clock::now() - started;
    const auto input_stats = AnalyzeAuditionSignal(selected_input, config->block_size);
    const auto output_stats = AnalyzeAuditionSignal(selected_output, config->block_size);
    for (std::uint32_t frame = 0U; frame < config->block_size; ++frame) {
        input_waveform[frame] = selected_input.ChannelData(0U)[frame];
        output_waveform[frame] = selected_output.ChannelData(0U)[frame];
    }
    const auto audio_seconds = static_cast<double>(config->block_size) / config->sample_rate_hz;
    const auto elapsed_seconds = std::chrono::duration<double>(elapsed).count();
    *out_analysis = SoloRigModuleAuditionAnalysisV3{
        .schema_version = 3U,
        .sample_rate_hz = config->sample_rate_hz,
        .block_size = config->block_size,
        .channel_count = config->channel_count,
        .declared_latency_samples = declared_latency,
        .waveform_sample_count = config->block_size,
        .input_peak = input_stats.peak,
        .input_rms = input_stats.rms,
        .input_dc = input_stats.dc,
        .output_peak = output_stats.peak,
        .output_rms = output_stats.rms,
        .output_dc = output_stats.dc,
        .processing_cpu_percent = audio_seconds > 0.0 ? elapsed_seconds / audio_seconds * 100.0 : 0.0,
        .output_finite = static_cast<std::uint8_t>(output_stats.finite ? 1U : 0U),
        .solver_processed_sample_count = solver_diagnostics.processed_solve_count,
        .solver_total_iterations = solver_diagnostics.total_solver_iterations,
        .solver_maximum_iterations = solver_diagnostics.maximum_solver_iterations,
        .solver_failure_count = solver_diagnostics.solve_failure_count,
        .solver_last_error = solver_diagnostics.last_solver_error,
    };
    if (!input_stats.finite || !output_stats.finite) {
        return SetError(backend, SOLO_RIG_STATUS_INTERNAL_ERROR, "offline audition produced non-finite output");
    }
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_audition_module_package_v3_utf8(
    SoloRigBackend* backend,
    const char* directory_path_utf8,
    const SoloRigModuleAuditionConfigV2* config,
    float* input_waveform,
    float* output_waveform,
    uint32_t waveform_capacity,
    SoloRigModuleAuditionAnalysisV3* out_analysis) {
    return AuditionModulePackageV3Impl(backend, directory_path_utf8, config,
        input_waveform, output_waveform, waveform_capacity, nullptr, 0.0, false, out_analysis);
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_audition_module_package_v4_utf8(
    SoloRigBackend* backend,
    const char* directory_path_utf8,
    const SoloRigModuleAuditionConfigV2* config,
    const char* parameter_id_utf8,
    double parameter_value,
    float* input_waveform,
    float* output_waveform,
    uint32_t waveform_capacity,
    SoloRigModuleAuditionAnalysisV3* out_analysis) {
    return AuditionModulePackageV3Impl(backend, directory_path_utf8, config,
        input_waveform, output_waveform, waveform_capacity, parameter_id_utf8,
        parameter_value, true, out_analysis);
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_audition_module_package_v2_utf8(
    SoloRigBackend* backend,
    const char* directory_path_utf8,
    const SoloRigModuleAuditionConfigV2* config,
    float* input_waveform,
    float* output_waveform,
    uint32_t waveform_capacity,
    SoloRigModuleAuditionAnalysisV2* out_analysis) {
    if (out_analysis == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "an audition analysis output is required");
    }
    SoloRigModuleAuditionAnalysisV3 extended{};
    const auto status = solorig_backend_audition_module_package_v3_utf8(
        backend, directory_path_utf8, config, input_waveform, output_waveform,
        waveform_capacity, &extended);
    if (status != SOLO_RIG_STATUS_OK) return status;
    *out_analysis = SoloRigModuleAuditionAnalysisV2{
        .schema_version = 2U,
        .sample_rate_hz = extended.sample_rate_hz,
        .block_size = extended.block_size,
        .channel_count = extended.channel_count,
        .declared_latency_samples = extended.declared_latency_samples,
        .waveform_sample_count = extended.waveform_sample_count,
        .input_peak = extended.input_peak,
        .input_rms = extended.input_rms,
        .input_dc = extended.input_dc,
        .output_peak = extended.output_peak,
        .output_rms = extended.output_rms,
        .output_dc = extended.output_dc,
        .processing_cpu_percent = extended.processing_cpu_percent,
        .output_finite = extended.output_finite,
    };
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_asio_refresh(SoloRigBackend* backend) {
    if (!ValidBackend(backend) || backend->audio_engine == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "audio backend is unavailable");
    }
    static_cast<void>(backend->audio_engine->RefreshDrivers());
    SyncAudioStatus(*backend);
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_asio_driver_count(
    SoloRigBackend* backend, uint32_t* out_count) {
    if (!ValidBackend(backend) || backend->audio_engine == nullptr || out_count == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                        "audio backend and driver count are required");
    }
    *out_count = static_cast<uint32_t>(backend->audio_engine->Drivers().size());
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_asio_driver_entry(
    SoloRigBackend* backend, uint32_t index, SoloRigAsioDriverView* out_driver) {
    if (!ValidBackend(backend) || backend->audio_engine == nullptr || out_driver == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                        "audio backend and driver entry are required");
    }
    const auto& drivers = backend->audio_engine->Drivers();
    if (index >= drivers.size()) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "ASIO driver entry was not found");
    }
    const auto& driver = drivers[index];
    const auto probe = solo_rig::standalone::AsioHost::ProbeDriver(driver, nullptr);
    out_driver->driver_id_utf8 = driver.driver_id.c_str();
    out_driver->display_name_utf8 = driver.display_name.c_str();
    out_driver->input_channel_count = probe.succeeded ? probe.device.input_channel_count : 0U;
    out_driver->output_channel_count = probe.succeeded ? probe.device.output_channel_count : 0U;
    out_driver->native_sample_rate_hz = probe.succeeded ? probe.device.native_sample_rate_hz : 0.0;
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_asio_open(
    SoloRigBackend* backend,
    uint32_t driver_index,
    void* native_window,
    uint32_t input_channel_0,
    uint32_t input_channel_1,
    uint32_t output_channel_0,
    uint32_t output_channel_1) {
    if (input_channel_0 == input_channel_1 || output_channel_0 == output_channel_1) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                        "selected ASIO input/output ports are invalid or duplicated");
    }
    const SoloRigAsioChannelRoute input_routes[] = {
        {.driver_index = driver_index, .device_channel = input_channel_0, .processing_channel = 0U},
        {.driver_index = driver_index, .device_channel = input_channel_1, .processing_channel = 1U},
    };
    const SoloRigAsioChannelRoute output_routes[] = {
        {.driver_index = driver_index, .device_channel = output_channel_0, .processing_channel = 0U},
        {.driver_index = driver_index, .device_channel = output_channel_1, .processing_channel = 1U},
    };
    return OpenAsioRouteMap(backend, native_window, input_routes, 2U, output_routes, 2U);
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_asio_open_routed(
    SoloRigBackend* backend,
    void* native_window,
    const SoloRigAsioChannelRoute* input_routes,
    uint32_t input_route_count,
    const SoloRigAsioChannelRoute* output_routes,
    uint32_t output_route_count) {
    return OpenAsioRouteMap(backend, native_window, input_routes, input_route_count,
                            output_routes, output_route_count);
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_asio_start(SoloRigBackend* backend) {
    if (!ValidBackend(backend) || backend->audio_engine == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "audio backend is unavailable");
    }
    if (!backend->audio_engine->Start()) {
        return SetError(backend, SOLO_RIG_STATUS_INTERNAL_ERROR, backend->audio_engine->ErrorText());
    }
    SyncAudioStatus(*backend);
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_asio_stop(SoloRigBackend* backend) {
    if (!ValidBackend(backend) || backend->audio_engine == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "audio backend is unavailable");
    }
    backend->audio_engine->Stop();
    SyncAudioStatus(*backend);
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_asio_close(SoloRigBackend* backend) {
    if (!ValidBackend(backend) || backend->audio_engine == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "audio backend is unavailable");
    }
    backend->audio_engine->Close();
    SyncAudioStatus(*backend);
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_asio_recover(SoloRigBackend* backend) {
    if (!ValidBackend(backend) || backend->audio_engine == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "audio backend is unavailable");
    }
    if (!backend->audio_engine->RecoverAudio()) {
        return SetError(backend, SOLO_RIG_STATUS_INTERNAL_ERROR, backend->audio_engine->ErrorText());
    }
    SyncAudioStatus(*backend);
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_tuner_set_enabled(
    SoloRigBackend* backend, uint8_t enabled) {
    if (!ValidBackend(backend) || backend->audio_engine == nullptr || enabled > 1U) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "tuner state and audio backend are required");
    }
    backend->audio_engine->SetTunerEnabled(enabled != 0U);
    SyncAudioStatus(*backend);
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_refresh(SoloRigBackend* backend) {
    if (!ValidBackend(backend) || backend->midi_session == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "MIDI session is unavailable");
    }
    static_cast<void>(backend->midi_session->RefreshDevices());
    SyncMidiStatus(*backend);
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_device_count(
    SoloRigBackend* backend, uint32_t* out_count) {
    if (!ValidBackend(backend) || backend->midi_session == nullptr || out_count == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                        "MIDI session and device count are required");
    }
    *out_count = static_cast<uint32_t>(backend->midi_session->Devices().size());
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_device_entry(
    SoloRigBackend* backend, uint32_t index, SoloRigMidiDeviceView* out_device) {
    if (!ValidBackend(backend) || backend->midi_session == nullptr || out_device == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                        "MIDI session and device entry are required");
    }
    SyncMidiStatus(*backend);
    const auto& devices = backend->midi_session->Devices();
    if (index >= devices.size() || index >= backend->midi_device_names_utf8.size()) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "MIDI device entry was not found");
    }
    out_device->device_index = devices[index].index;
    out_device->display_name_utf8 = backend->midi_device_names_utf8[index].c_str();
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_open(
    SoloRigBackend* backend, uint32_t device_index) {
    if (!ValidBackend(backend) || backend->midi_session == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "MIDI session is unavailable");
    }
    if (!backend->midi_session->Open(device_index)) {
        return SetError(backend, SOLO_RIG_STATUS_INTERNAL_ERROR, backend->midi_session->StatusText());
    }
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_close(SoloRigBackend* backend) {
    if (!ValidBackend(backend) || backend->midi_session == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "MIDI session is unavailable");
    }
    backend->midi_session->Close();
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_drain(
    SoloRigBackend* backend, uint32_t* out_processed_messages) {
    if (!ValidBackend(backend) || backend->midi_session == nullptr || out_processed_messages == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                        "MIDI session and processed message count are required");
    }
    const auto previous = CaptureEditorHistory(*backend);
    *out_processed_messages = static_cast<uint32_t>(backend->midi_session->Drain());
    if (EditorPresetChanged(previous.preset, backend->editor.ExportPreset())) {
        CommitEditorEdit(*backend, previous);
    }
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_target_count(
    SoloRigBackend* backend, uint32_t* out_count) {
    if (!ValidBackend(backend) || backend->midi_session == nullptr || out_count == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                        "MIDI session and target count are required");
    }
    SyncMidiLearnTargets(*backend);
    *out_count = static_cast<uint32_t>(backend->midi_learn_targets.size());
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_target_entry(
    SoloRigBackend* backend, uint32_t index, SoloRigMidiLearnTargetView* out_target) {
    if (!ValidBackend(backend) || backend->midi_session == nullptr || out_target == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                        "MIDI session and target entry are required");
    }
    SyncMidiLearnTargets(*backend);
    if (index >= backend->midi_learn_targets.size()) {
        return SetError(backend, SOLO_RIG_STATUS_NOT_FOUND, "MIDI learn target was not found");
    }
    const auto& target = backend->midi_learn_targets[index];
    out_target->target_id_utf8 = target.target_id.c_str();
    out_target->display_name_utf8 = target.display_name.c_str();
    out_target->minimum = target.minimum;
    out_target->maximum = target.maximum;
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_learn_arm(
    SoloRigBackend* backend, const char* target_id_utf8) {
    if (!ValidBackend(backend) || backend->midi_session == nullptr || !ValidText(target_id_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                        "MIDI session and a learn target are required");
    }
    if (!backend->midi_session->SyncFromEditor() ||
        !backend->midi_session->ArmLearn(target_id_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_INTERNAL_ERROR, backend->midi_session->StatusText());
    }
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_learn_cancel(SoloRigBackend* backend) {
    if (!ValidBackend(backend) || backend->midi_session == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "MIDI session is unavailable");
    }
    backend->midi_session->CancelLearn();
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_delete_binding(
    SoloRigBackend* backend, uint32_t index) {
    if (!ValidBackend(backend) || backend->midi_session == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "MIDI session is unavailable");
    }
    const auto previous = CaptureEditorHistory(*backend);
    if (!backend->midi_session->SyncFromEditor() || !backend->midi_session->RemoveBinding(index)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, backend->midi_session->StatusText());
    }
    CommitEditorEdit(*backend, std::move(previous));
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_clear_bindings(SoloRigBackend* backend) {
    if (!ValidBackend(backend) || backend->midi_session == nullptr) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT, "MIDI session is unavailable");
    }
    const auto previous = CaptureEditorHistory(*backend);
    if (!backend->midi_session->SyncFromEditor() || !backend->midi_session->ClearBindings()) {
        return SetError(backend, SOLO_RIG_STATUS_INTERNAL_ERROR, backend->midi_session->StatusText());
    }
    CommitEditorEdit(*backend, std::move(previous));
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}

SOLO_RIG_BACKEND_API SoloRigStatus solorig_backend_midi_import_bank_utf8(
    SoloRigBackend* backend, const char* directory_path_utf8) {
    if (!ValidBackend(backend) || backend->midi_session == nullptr || !ValidText(directory_path_utf8)) {
        return SetError(backend, SOLO_RIG_STATUS_INVALID_ARGUMENT,
                        "MIDI session and a preset bank directory are required");
    }
    const auto previous = CaptureEditorHistory(*backend);
    if (!backend->midi_session->SyncFromEditor() ||
        !backend->midi_session->LoadPresetBankDirectory(PathFromUtf8(directory_path_utf8))) {
        return SetError(backend, SOLO_RIG_STATUS_FILE_ERROR, backend->midi_session->StatusText());
    }
    CommitEditorEdit(*backend, std::move(previous));
    backend->last_error.clear();
    return SOLO_RIG_STATUS_OK;
}
