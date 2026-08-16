#include "standalone/standalone_audio_engine.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <utility>

namespace solo_rig::standalone {
namespace {

std::string TunerNoteName(int midi_note) {
    static constexpr std::array<const char*, 12> kNoteNames{
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (midi_note < 0 || midi_note > 127) {
        return "?";
    }
    const auto octave = midi_note / 12 - 1;
    return std::string(kNoteNames[static_cast<std::size_t>(midi_note % 12)]) + std::to_string(octave);
}

} // namespace

StandaloneAudioEngine::~StandaloneAudioEngine() {
    Close();
}

bool StandaloneAudioEngine::RefreshDrivers() {
    drivers_ = AsioHost::EnumerateDrivers();
    SetStatus("ASIO drivers discovered: " + std::to_string(drivers_.size()));
    error_text_.clear();
    return !drivers_.empty();
}

const std::vector<AsioDriverInfo>& StandaloneAudioEngine::Drivers() const noexcept {
    return drivers_;
}

bool StandaloneAudioEngine::Open(const AsioDriverInfo& driver, HWND system_window, RigPreset preset) {
    const auto probe = AsioHost::ProbeDriver(driver, system_window);
    if (!probe.succeeded) {
        Close();
        error_text_ = probe.driver_error;
        SetErrorStatus("ASIO probe failed");
        return false;
    }
    StandaloneAudioRouting routing{.processing_format = {.sample_rate_hz = 192000.0,
                                                        .max_block_size = 1U,
                                                        .channel_count = 2U,
                                                        .sample_representation = SampleRepresentation::Float32},
                                   .device_buffer_size = probe.preferred_buffer_size,
                                   .inputs = {{.device_id = probe.device.device_id,
                                               .device_channel = 0U,
                                               .processing_channel = 0U},
                                              {.device_id = probe.device.device_id,
                                               .device_channel = 1U,
                                               .processing_channel = 1U}},
                                   .outputs = {{.device_id = probe.device.device_id,
                                                .device_channel = 0U,
                                                .processing_channel = 0U},
                                               {.device_id = probe.device.device_id,
                                                .device_channel = 1U,
                                                .processing_channel = 1U}}};
    return OpenInternal({driver}, system_window, std::move(preset), std::move(routing));
}

bool StandaloneAudioEngine::OpenWithRouting(const AsioDriverInfo& driver,
                                            HWND system_window,
                                            RigPreset preset,
                                            StandaloneAudioRouting routing) {
    return OpenInternal({driver}, system_window, std::move(preset), std::move(routing));
}

bool StandaloneAudioEngine::OpenWithDevices(std::vector<AsioDriverInfo> drivers,
                                            HWND system_window,
                                            RigPreset preset,
                                            StandaloneAudioRouting routing) {
    return OpenInternal(std::move(drivers), system_window, std::move(preset), std::move(routing));
}

bool StandaloneAudioEngine::OpenInternal(std::vector<AsioDriverInfo> drivers,
                                         HWND system_window,
                                         RigPreset preset,
                                         StandaloneAudioRouting routing) {
    Close();
    if (drivers.empty()) {
        error_text_ = "no ASIO drivers were supplied";
        SetErrorStatus("ASIO routing rejected");
        return false;
    }
    if (!asio_coordinator_.Open(drivers, system_window, std::move(routing))) {
        error_text_ = asio_coordinator_.LastErrorMessage();
        asio_coordinator_.Close();
        SetErrorStatus("ASIO open failed");
        return false;
    }
    routing_ = asio_coordinator_.Routing();
    format_ = routing_.processing_format;
    if (!BuildRuntime(preset, format_)) {
        error_text_ = "selected preset could not be prepared for the ASIO format";
        SetErrorStatus("Standalone graph rejected");
        asio_coordinator_.Close();
        return false;
    }
    asio_coordinator_.SetProcessCallback(&StandaloneAudioEngine::ProcessCallback, this);
    error_text_.clear();
    callback_count_.store(0U, std::memory_order_relaxed);
    last_callback_sample_count_.store(0U, std::memory_order_relaxed);
    const auto native_rate = static_cast<std::uint32_t>(std::lround(Device().native_sample_rate_hz));
    SetStatus("ASIO open: " + Device().display_name + " | device " +
              std::to_string(native_rate) + " Hz -> internal 192 kHz | " +
              std::to_string(asio_coordinator_.MasterBufferSize()) + " samples | " +
              std::to_string(asio_coordinator_.ActiveDeviceCount()) + " device(s) | " +
              std::to_string(format_.channel_count) + "-channel graph");
    return true;
}

bool StandaloneAudioEngine::Start() {
    if (!asio_coordinator_.Start()) {
        error_text_ = asio_coordinator_.LastErrorMessage();
        SetErrorStatus("ASIO start failed");
        return false;
    }
    error_text_.clear();
    SetStatus("ASIO running: " + Device().display_name + " | device " +
              std::to_string(static_cast<std::uint32_t>(std::lround(Device().native_sample_rate_hz))) +
              " Hz -> internal 192 kHz | " +
              std::to_string(asio_coordinator_.MasterBufferSize()) + " samples | " +
              std::to_string(asio_coordinator_.ActiveDeviceCount()) + " device(s)");
    return true;
}

void StandaloneAudioEngine::Stop() {
    asio_coordinator_.Stop();
    if (asio_coordinator_.IsOpen()) {
        SetStatus("ASIO stopped: " + Device().display_name);
    }
}

void StandaloneAudioEngine::Close() noexcept {
    asio_coordinator_.Close();
    ReleaseRuntime();
    module_registry_.ClearExternalPackages();
    active_preset_.reset();
    format_ = {};
    routing_ = {};
    processing_input_ = {};
    processing_output_ = {};
    try {
        if (drivers_.empty()) {
            status_text_ = "ASIO backend not configured/available";
        } else {
            status_text_ = "ASIO closed | drivers discovered: " + std::to_string(drivers_.size());
        }
    } catch (...) {
        status_text_.clear();
    }
}

bool StandaloneAudioEngine::ReloadPreset(RigPreset preset) {
    if (!asio_coordinator_.IsOpen() || format_.Validate().size() != 0U) {
        return false;
    }
    const auto was_running = asio_coordinator_.IsRunning();
    const auto previous_preset = active_preset_;
    const auto restore_previous = [&]() {
        ReleaseRuntime();
        if (!previous_preset.has_value() || !BuildRuntime(*previous_preset, format_)) {
            return false;
        }
        if (was_running && !asio_coordinator_.Start()) {
            ReleaseRuntime();
            return false;
        }
        return true;
    };
    asio_coordinator_.Stop();
    ReleaseRuntime();
    if (!BuildRuntime(preset, format_)) {
        error_text_ = "selected preset could not be prepared for the ASIO format";
        if (restore_previous()) {
            SetErrorStatus("Standalone graph reload failed; previous graph restored");
            return false;
        }
        SetErrorStatus("Standalone graph reload failed; previous graph could not be restored");
        return false;
    }
    if (was_running && !asio_coordinator_.Start()) {
        error_text_ = asio_coordinator_.LastErrorMessage();
        if (restore_previous()) {
            SetErrorStatus("ASIO restart failed after graph reload; previous graph restored");
            return false;
        }
        SetErrorStatus("ASIO restart failed after graph reload; previous graph could not be restored");
        return false;
    }
    error_text_.clear();
    SetStatus(was_running ? "ASIO running with reloaded graph" : "ASIO open with reloaded graph");
    return true;
}

bool StandaloneAudioEngine::SetNodeBypassed(std::string_view instance_id, bool bypassed) {
    if (!active_preset_.has_value() || instance_id.empty()) {
        return false;
    }
    auto candidate = *active_preset_;
    const auto node = std::ranges::find_if(candidate.nodes, [instance_id](const PresetNode& item) {
        return item.instance_id == instance_id;
    });
    if (node == candidate.nodes.end()) {
        return false;
    }
    node->bypassed = bypassed;
    if (asio_coordinator_.IsOpen()) {
        return ReloadPreset(std::move(candidate));
    }
    if (format_.Validate().size() != 0U || !prepared_runtime_.has_value() || !rig_runtime_.has_value()) {
        return false;
    }
    const auto previous = *active_preset_;
    ReleaseRuntime();
    if (BuildRuntime(candidate, format_)) {
        error_text_.clear();
        SetStatus(bypassed ? "Standalone graph bypassed node" : "Standalone graph enabled node");
        return true;
    }
    ReleaseRuntime();
    static_cast<void>(BuildRuntime(previous, format_));
    SetErrorStatus("Standalone graph bypass change failed; previous graph restored");
    return false;
}

bool StandaloneAudioEngine::RecoverAudio() {
    if (!asio_coordinator_.ResetRequested() && !asio_coordinator_.DropoutReported()) {
        return false;
    }
    if (!asio_coordinator_.Recover()) {
        error_text_ = asio_coordinator_.LastErrorMessage();
        SetErrorStatus("ASIO recovery failed");
        return false;
    }
    error_text_.clear();
    SetStatus("ASIO recovered: " + Device().display_name + " | " +
              std::to_string(asio_coordinator_.ActiveDeviceCount()) + " device(s)");
    return true;
}

void StandaloneAudioEngine::SetTunerEnabled(bool enabled) noexcept {
    tuner_write_index_.store(0U, std::memory_order_release);
    tuner_enabled_.store(enabled, std::memory_order_release);
}

bool StandaloneAudioEngine::TunerEnabled() const noexcept {
    return tuner_enabled_.load(std::memory_order_acquire);
}

std::optional<TuningResult> StandaloneAudioEngine::AnalyzeTuner() const {
    if (!TunerEnabled()) {
        return std::nullopt;
    }
    const auto end = tuner_write_index_.load(std::memory_order_acquire);
    if (end < kTunerWindowSamples || format_.sample_rate_hz <= 0.0) {
        return std::nullopt;
    }
    std::array<float, kTunerWindowSamples> snapshot{};
    const auto start = end - kTunerWindowSamples;
    for (std::size_t index = 0U; index < kTunerWindowSamples; ++index) {
        snapshot[index] = tuner_samples_[(start + index) % kTunerWindowSamples].load(
            std::memory_order_acquire);
    }
    if (format_.sample_rate_hz >= 96000.0) {
        constexpr std::size_t kDecimation = 4U;
        std::array<float, kTunerWindowSamples / kDecimation> decimated{};
        for (std::size_t index = 0U; index < decimated.size(); ++index) {
            float sum = 0.0F;
            for (std::size_t offset = 0U; offset < kDecimation; ++offset) {
                sum += snapshot[index * kDecimation + offset];
            }
            decimated[index] = sum / static_cast<float>(kDecimation);
        }
        return TunerAnalyzer{}.Analyze(decimated, format_.sample_rate_hz / kDecimation);
    }
    return TunerAnalyzer{}.Analyze(snapshot, format_.sample_rate_hz);
}

bool StandaloneAudioEngine::RunTunerCaptureSelfTest() {
    StandaloneAudioEngine engine;
    engine.format_ = AudioFormat{.sample_rate_hz = 192000.0,
                                 .max_block_size = static_cast<std::uint32_t>(kTunerWindowSamples),
                                 .channel_count = 1U,
                                 .sample_representation = SampleRepresentation::Float32};
    engine.SetTunerEnabled(true);
    std::array<float, kTunerWindowSamples> input{};
    std::array<float, kTunerWindowSamples> output{};
    constexpr double frequency_hz = 440.0;
    for (std::size_t index = 0U; index < input.size(); ++index) {
        input[index] = static_cast<float>(0.25 * std::sin(
            2.0 * std::numbers::pi * frequency_hz * static_cast<double>(index) / 192000.0));
    }
    const float* inputs[] = {input.data()};
    float* outputs[] = {output.data()};
    engine.ProcessBlock(inputs, outputs, 1U, static_cast<std::uint32_t>(input.size()));
    const auto result = engine.AnalyzeTuner();
    return result.has_value() && std::isfinite(result->frequency_hz) &&
        std::fabs(result->frequency_hz - frequency_hz) < 5.0 && result->confidence >= 0.6;
}

bool StandaloneAudioEngine::RunExternalModulePackageRuntimeSelfTest() {
    const auto directory = std::filesystem::temp_directory_path() / "solorig-standalone-module-package";
    std::error_code cleanup;
    std::filesystem::remove_all(directory, cleanup);
    try {
        std::filesystem::create_directories(directory / "logic");
        {
            std::ofstream manifest(directory / "manifest.json", std::ios::binary | std::ios::trunc);
            manifest << R"({
                "schemaVersion":1,
                "moduleId":"org.mumu-dada.solorig.standalone-package",
                "displayName":"Standalone Package",
                "category":"digital-algorithm",
                "logicFile":"logic/main.json",
                "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}],
                "parameters":[{"id":"gain","minimum":0,"maximum":2,"default":0.25}]
            })";
            std::ofstream logic(directory / "logic/main.json", std::ios::binary | std::ios::trunc);
            logic << R"({
                "schemaVersion":1,
                "kind":"digital-algorithm",
                "algorithm":"linear-gain",
                "operations":[{"type":"gain","parameter":"gain"}]
            })";
        }
        const auto absolute_root = std::filesystem::absolute(directory, cleanup).lexically_normal();
        if (cleanup) {
            std::filesystem::remove_all(directory, cleanup);
            return false;
        }
        RigPreset preset{
            .preset_id = "standalone-package-self-test",
            .name = "Standalone Package Self Test",
            .nodes = {{.instance_id = "package-1",
                       .module_id = "org.mumu-dada.solorig.standalone-package",
                       .parameter_values = {{"gain", 0.25F}}}},
            .external_module_packages = {{.module_id = "org.mumu-dada.solorig.standalone-package",
                                          .root_directory = absolute_root}},
        };
        StandaloneAudioEngine engine;
        const AudioFormat format{.sample_rate_hz = 192000.0,
                                 .max_block_size = 64U,
                                 .channel_count = 1U,
                                 .sample_representation = SampleRepresentation::Float32};
        if (!engine.BuildRuntime(preset, format)) {
            std::filesystem::remove_all(directory, cleanup);
            return false;
        }
        std::array<float, 64> input{};
        std::array<float, 64> output{};
        input[0] = 1.0F;
        const float* inputs[] = {input.data()};
        float* outputs[] = {output.data()};
        engine.ProcessBlock(inputs, outputs, 1U, 1U);
        const auto valid = std::fabs(output[0] - 0.25F) < 0.0001F;
        if (!engine.SetNodeBypassed("package-1", true)) {
            std::filesystem::remove_all(directory, cleanup);
            return false;
        }
        input[0] = 1.0F;
        output[0] = 0.0F;
        engine.ProcessBlock(inputs, outputs, 1U, 1U);
        const auto bypassed = std::fabs(output[0] - 1.0F) < 0.0001F;
        if (!engine.active_preset_.has_value() || engine.active_preset_->preset_id != preset.preset_id) {
            std::filesystem::remove_all(directory, cleanup);
            return false;
        }
        engine.ReleaseRuntime();
        auto invalid_preset = preset;
        invalid_preset.external_module_packages.front().root_directory = directory / "missing-module";
        if (engine.BuildRuntime(invalid_preset, format) || !engine.active_preset_.has_value() ||
            engine.active_preset_->preset_id != preset.preset_id || !engine.BuildRuntime(*engine.active_preset_, format)) {
            std::filesystem::remove_all(directory, cleanup);
            return false;
        }
        engine.ReleaseRuntime();
        const std::array<std::uint8_t, 48> wave{
            'R', 'I', 'F', 'F', 40U, 0U, 0U, 0U, 'W', 'A', 'V', 'E',
            'f', 'm', 't', ' ', 16U, 0U, 0U, 0U, 1U, 0U, 1U, 0U,
            0x80U, 0xBBU, 0U, 0U, 0U, 0x77U, 1U, 0U, 2U, 0U, 16U, 0U,
            'd', 'a', 't', 'a', 4U, 0U, 0U, 0U, 0xFFU, 0x7FU, 0U, 0U};
        {
            std::ofstream output_wave(directory / "direct.wav", std::ios::binary | std::ios::trunc);
            output_wave.write(reinterpret_cast<const char*>(wave.data()),
                              static_cast<std::streamsize>(wave.size()));
        }
        const auto direct_asset = std::filesystem::absolute(directory / "direct.wav", cleanup).lexically_normal();
        if (cleanup) {
            std::filesystem::remove_all(directory, cleanup);
            return false;
        }
        RigPreset direct_preset{
            .preset_id = "standalone-direct-asset-self-test",
            .name = "Standalone Direct Asset Self Test",
            .nodes = {{.instance_id = "direct-ir-1",
                       .module_id = "org.mumu-dada.solorig.imported.direct-ir"}},
            .external_module_packages = {{.module_id = "org.mumu-dada.solorig.imported.direct-ir",
                                          .root_directory = direct_asset.parent_path(),
                                          .source_kind = ExternalModuleSourceKind::ImpulseResponse,
                                          .asset_path = direct_asset}},
        };
        StandaloneAudioEngine direct_engine;
        if (!direct_engine.BuildRuntime(direct_preset, format)) {
            std::filesystem::remove_all(directory, cleanup);
            return false;
        }
        std::array<float, 64> direct_input{};
        std::array<float, 64> direct_output{};
        direct_input[0] = 1.0F;
        const float* direct_inputs[] = {direct_input.data()};
        float* direct_outputs[] = {direct_output.data()};
        direct_engine.ProcessBlock(direct_inputs, direct_outputs, 1U, 2U);
        direct_engine.ReleaseRuntime();
        std::filesystem::remove_all(directory, cleanup);
        return valid && bypassed && std::isfinite(direct_output[0]) && direct_output[0] > 0.5F;
    } catch (...) {
        std::filesystem::remove_all(directory, cleanup);
        return false;
    }
}

bool StandaloneAudioEngine::BuildRuntime(const RigPreset& preset, const AudioFormat& format) {
    try {
        auto candidate = preset;
        if (candidate.preset_id.empty()) {
            candidate.preset_id = "standalone-editor";
        }
        module_registry_.ClearExternalPackages();
        for (const auto& reference : candidate.external_module_packages) {
            auto root = reference.root_directory;
            auto asset = reference.asset_path;
            std::error_code path_error;
            root = std::filesystem::absolute(root, path_error).lexically_normal();
            if (path_error || root.empty()) {
                return false;
            }
            if (asset.is_relative()) {
                asset = root / asset;
            }
            const auto loaded = reference.source_kind == ExternalModuleSourceKind::PackageDirectory
                ? LoadModulePackage(root)
                : LoadExternalModuleAsset(asset, reference.source_kind, reference.module_id, {});
            if (!loaded.package.has_value() || !loaded.errors.empty() ||
                loaded.package->descriptor.module_id != reference.module_id) {
                return false;
            }
            auto package = std::move(*loaded.package);
            const auto relative_skin = package.custom_skin_path.has_value()
                ? package.custom_skin_path->lexically_relative(package.root_directory)
                : std::filesystem::path{};
            package.root_directory = reference.source_kind == ExternalModuleSourceKind::PackageDirectory
                ? root : asset.parent_path();
            package.logic_path = reference.source_kind == ExternalModuleSourceKind::PackageDirectory
                ? root / package.descriptor.logic_entry_point : asset;
            if (reference.source_kind != ExternalModuleSourceKind::PackageDirectory) {
                if (auto* model = std::get_if<NeuralAmpModelLogic>(&package.logic)) model->model_path = asset;
                if (auto* impulse = std::get_if<ImpulseResponseLogic>(&package.logic)) impulse->impulse_response_path = asset;
            }
            if (!relative_skin.empty()) {
                package.custom_skin_path = root / relative_skin;
            }
            if (module_registry_.RegisterPackage(std::move(package)).has_value()) {
                return false;
            }
        }
        PreparedPreset prepared{.preset_id = candidate.preset_id, .preset = std::move(candidate)};
        const auto built = RigRuntimeBuilder{}.Build(prepared, module_registry_, format);
        if (std::holds_alternative<RigBuildError>(built)) {
            return false;
        }
        auto built_preset = std::get<PreparedPreset>(built);
        processing_input_ = AudioBuffer(format.channel_count, format.max_block_size);
        processing_output_ = AudioBuffer(format.channel_count, format.max_block_size);
        prepared_runtime_.emplace(std::move(built_preset));
        rig_runtime_.emplace(mailbox_, format);
        if (!processing_input_.IsValid() || !processing_output_.IsValid() || !rig_runtime_->Prepare()) {
            ReleaseRuntime();
            return false;
        }
        mailbox_.Submit(*prepared_runtime_);
        active_preset_ = prepared_runtime_->preset;
        format_ = format;
        return true;
    } catch (...) {
        ReleaseRuntime();
        return false;
    }
}

void StandaloneAudioEngine::ReleaseRuntime() noexcept {
    if (rig_runtime_.has_value()) {
        rig_runtime_->Shutdown();
        rig_runtime_.reset();
    } else {
        mailbox_.ResetForReconfigure();
    }
    prepared_runtime_.reset();
}

void StandaloneAudioEngine::SetStatus(std::string status) {
    status_text_ = std::move(status);
}

void StandaloneAudioEngine::SetErrorStatus(std::string prefix) {
    status_text_ = std::move(prefix);
    if (!error_text_.empty()) {
        status_text_ += ": ";
        status_text_ += error_text_;
    }
}

void StandaloneAudioEngine::ProcessCallback(void* user,
                                            const float* const* inputs,
                                            float* const* outputs,
                                            std::uint32_t channel_count,
    std::uint32_t sample_count) noexcept {
    if (user != nullptr) {
        auto* engine = static_cast<StandaloneAudioEngine*>(user);
        engine->callback_count_.fetch_add(1U, std::memory_order_relaxed);
        engine->last_callback_sample_count_.store(sample_count, std::memory_order_relaxed);
        engine->ProcessBlock(inputs, outputs, channel_count, sample_count);
    }
}

void StandaloneAudioEngine::ProcessBlock(const float* const* inputs,
                                         float* const* outputs,
                                         std::uint32_t channel_count,
                                         std::uint32_t sample_count) noexcept {
    if (tuner_enabled_.load(std::memory_order_relaxed) && channel_count > 0U) {
        const auto write = tuner_write_index_.load(std::memory_order_relaxed);
        const auto* source = inputs == nullptr ? nullptr : inputs[0];
        for (std::uint32_t sample = 0U; sample < sample_count; ++sample) {
            tuner_samples_[(write + sample) % kTunerWindowSamples].store(
                source == nullptr ? 0.0F : source[sample], std::memory_order_relaxed);
        }
        tuner_write_index_.store(write + sample_count, std::memory_order_release);
    }
    if (!rig_runtime_.has_value() || !rig_runtime_->IsPrepared() ||
        channel_count != format_.channel_count || sample_count > format_.max_block_size) {
        for (std::uint32_t channel = 0U; channel < channel_count; ++channel) {
            if (outputs != nullptr && outputs[channel] != nullptr) {
                std::fill_n(outputs[channel], sample_count, 0.0F);
            }
        }
        return;
    }
    for (std::uint32_t channel = 0U; channel < channel_count; ++channel) {
        const auto* source = inputs == nullptr ? nullptr : inputs[channel];
        if (source == nullptr) {
            std::fill_n(processing_input_.ChannelData(channel), sample_count, 0.0F);
        } else {
            std::copy_n(source, sample_count, processing_input_.ChannelData(channel));
        }
    }
    if (!rig_runtime_->Process(processing_input_, processing_output_, sample_count)) {
        processing_output_.Clear(sample_count);
    }
    for (std::uint32_t channel = 0U; channel < channel_count; ++channel) {
        if (outputs != nullptr && outputs[channel] != nullptr) {
            std::copy_n(processing_output_.ChannelData(channel), sample_count, outputs[channel]);
        }
    }
}

bool StandaloneAudioEngine::IsOpen() const noexcept { return asio_coordinator_.IsOpen(); }

bool StandaloneAudioEngine::IsRunning() const noexcept { return asio_coordinator_.IsRunning(); }

const AsioDeviceDescriptor& StandaloneAudioEngine::Device() const noexcept {
    return asio_coordinator_.MasterDevice();
}

const std::string& StandaloneAudioEngine::StatusText() const noexcept { return status_text_; }

std::string StandaloneAudioEngine::RuntimeStatusText() const {
    auto status = status_text_;
    if (IsRunning()) {
        status += " | callbacks " + std::to_string(CallbackCount());
        const auto last_block = LastCallbackSampleCount();
        if (last_block != 0U) {
            status += " / last " + std::to_string(last_block) + " frames";
        }
    }
    if (ResetRequested()) {
        status += " | ASIO reset requested";
    }
    if (DropoutReported()) {
        status += " | dropout detected; recover required";
    }
    if (TunerEnabled()) {
        const auto tuning = AnalyzeTuner();
        if (!tuning.has_value()) {
            status += " | tuner: no stable signal";
        } else {
            const auto sign = tuning->cents_from_nearest_note >= 0.0 ? "+" : "-";
            status += " | tuner: " + TunerNoteName(tuning->midi_note) + " " + sign +
                std::to_string(std::fabs(tuning->cents_from_nearest_note)) + " cents";
        }
    }
    return status;
}

const std::string& StandaloneAudioEngine::ErrorText() const noexcept { return error_text_; }

std::uint32_t StandaloneAudioEngine::BufferSize() const noexcept {
    return asio_coordinator_.MasterBufferSize();
}

std::uint32_t StandaloneAudioEngine::InputLatencySamples() const noexcept {
    return asio_coordinator_.MasterInputLatencySamples();
}

std::uint32_t StandaloneAudioEngine::OutputLatencySamples() const noexcept {
    return asio_coordinator_.MasterOutputLatencySamples();
}

bool StandaloneAudioEngine::ResetRequested() const noexcept { return asio_coordinator_.ResetRequested(); }

bool StandaloneAudioEngine::DropoutReported() const noexcept { return asio_coordinator_.DropoutReported(); }

std::uint64_t StandaloneAudioEngine::CallbackCount() const noexcept {
    return callback_count_.load(std::memory_order_relaxed);
}

std::uint32_t StandaloneAudioEngine::LastCallbackSampleCount() const noexcept {
    return last_callback_sample_count_.load(std::memory_order_relaxed);
}

const StandaloneAudioRouting& StandaloneAudioEngine::Routing() const noexcept {
    return routing_;
}

} // namespace solo_rig::standalone
