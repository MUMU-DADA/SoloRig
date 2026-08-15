#include "standalone/standalone_audio_engine.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace solo_rig::standalone {

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
    asio_coordinator_.Stop();
    ReleaseRuntime();
    if (!BuildRuntime(preset, format_)) {
        error_text_ = "selected preset could not be prepared for the ASIO format";
        SetErrorStatus("Standalone graph reload failed");
        return false;
    }
    if (was_running && !asio_coordinator_.Start()) {
        error_text_ = asio_coordinator_.LastErrorMessage();
        SetErrorStatus("ASIO restart failed after graph reload");
        return false;
    }
    error_text_.clear();
    SetStatus(was_running ? "ASIO running with reloaded graph" : "ASIO open with reloaded graph");
    return true;
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

bool StandaloneAudioEngine::BuildRuntime(const RigPreset& preset, const AudioFormat& format) {
    try {
        auto candidate = preset;
        if (candidate.preset_id.empty()) {
            candidate.preset_id = "standalone-editor";
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
