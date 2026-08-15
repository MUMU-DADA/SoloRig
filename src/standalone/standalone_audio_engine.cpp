#include "standalone/standalone_audio_engine.hpp"
#include "standalone/audio_resampler.hpp"

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
    Close();
    const auto probe = AsioHost::ProbeDriver(driver, system_window);
    if (!probe.succeeded) {
        error_text_ = probe.driver_error;
        SetErrorStatus("ASIO probe failed");
        return false;
    }
    if (probe.device.input_channel_count < 2U || probe.device.output_channel_count < 2U) {
        error_text_ = "Standalone requires at least two input and two output channels";
        SetErrorStatus("ASIO routing rejected");
        return false;
    }

    format_ = {.sample_rate_hz = 192000.0,
               .max_block_size = static_cast<std::uint32_t>(std::ceil(
                   static_cast<double>(probe.preferred_buffer_size) * 192000.0 /
                   probe.device.native_sample_rate_hz)) + 2U,
               .channel_count = 2U,
               .sample_representation = SampleRepresentation::Float32};
    if (!BuildRuntime(preset, format_)) {
        error_text_ = "selected preset could not be prepared for the ASIO format";
        SetErrorStatus("Standalone graph rejected");
        return false;
    }

    StandaloneAudioRouting routing{.processing_format = format_,
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
    if (!asio_host_.Open(driver, system_window, routing, &StandaloneAudioEngine::ProcessCallback, this)) {
        error_text_ = asio_host_.LastErrorMessage();
        asio_host_.Close();
        ReleaseRuntime();
        SetErrorStatus("ASIO open failed");
        return false;
    }
    error_text_.clear();
    callback_count_.store(0U, std::memory_order_relaxed);
    last_callback_sample_count_.store(0U, std::memory_order_relaxed);
    const auto native_rate = static_cast<std::uint32_t>(std::lround(probe.device.native_sample_rate_hz));
    SetStatus("ASIO open: " + probe.device.display_name + " | device " +
              std::to_string(native_rate) + " Hz -> internal 192 kHz | " +
              std::to_string(asio_host_.BufferSize()) + " samples | 2x2 graph");
    return true;
}

bool StandaloneAudioEngine::Start() {
    if (!asio_host_.Start()) {
        error_text_ = asio_host_.LastErrorMessage();
        SetErrorStatus("ASIO start failed");
        return false;
    }
    error_text_.clear();
    SetStatus("ASIO running: " + asio_host_.Device().display_name + " | device " +
              std::to_string(static_cast<std::uint32_t>(std::lround(asio_host_.Device().native_sample_rate_hz))) +
              " Hz -> internal 192 kHz | " +
              std::to_string(asio_host_.BufferSize()) + " samples");
    return true;
}

void StandaloneAudioEngine::Stop() {
    asio_host_.Stop();
    if (asio_host_.IsOpen()) {
        SetStatus("ASIO stopped: " + asio_host_.Device().display_name);
    }
}

void StandaloneAudioEngine::Close() noexcept {
    asio_host_.Close();
    ReleaseRuntime();
    format_ = {};
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
    if (!asio_host_.IsOpen() || format_.Validate().size() != 0U) {
        return false;
    }
    const auto was_running = asio_host_.IsRunning();
    asio_host_.Stop();
    ReleaseRuntime();
    if (!BuildRuntime(preset, format_)) {
        error_text_ = "selected preset could not be prepared for the ASIO format";
        SetErrorStatus("Standalone graph reload failed");
        return false;
    }
    if (was_running && !asio_host_.Start()) {
        error_text_ = asio_host_.LastErrorMessage();
        SetErrorStatus("ASIO restart failed after graph reload");
        return false;
    }
    error_text_.clear();
    SetStatus(was_running ? "ASIO running with reloaded graph" : "ASIO open with reloaded graph");
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
    const auto internal_sample_count = ResampledBlockSize(
        sample_count,
        asio_host_.Device().native_sample_rate_hz,
        format_.sample_rate_hz);
    if (!rig_runtime_.has_value() || !rig_runtime_->IsPrepared() ||
        channel_count != format_.channel_count || internal_sample_count > format_.max_block_size) {
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
            std::fill_n(processing_input_.ChannelData(channel), internal_sample_count, 0.0F);
        } else {
            static_cast<void>(ResampleLinear(
                std::span<const float>(source, sample_count),
                std::span<float>(processing_input_.ChannelData(channel), internal_sample_count),
                asio_host_.Device().native_sample_rate_hz,
                format_.sample_rate_hz));
        }
    }
    if (!rig_runtime_->Process(processing_input_, processing_output_, internal_sample_count)) {
        processing_output_.Clear(internal_sample_count);
    }
    for (std::uint32_t channel = 0U; channel < channel_count; ++channel) {
        if (outputs != nullptr && outputs[channel] != nullptr) {
            static_cast<void>(ResampleLinear(
                std::span<const float>(processing_output_.ChannelData(channel), internal_sample_count),
                std::span<float>(outputs[channel], sample_count),
                format_.sample_rate_hz,
                asio_host_.Device().native_sample_rate_hz));
        }
    }
}

bool StandaloneAudioEngine::IsOpen() const noexcept { return asio_host_.IsOpen(); }

bool StandaloneAudioEngine::IsRunning() const noexcept { return asio_host_.IsRunning(); }

const AsioDeviceDescriptor& StandaloneAudioEngine::Device() const noexcept { return asio_host_.Device(); }

const std::string& StandaloneAudioEngine::StatusText() const noexcept { return status_text_; }

const std::string& StandaloneAudioEngine::ErrorText() const noexcept { return error_text_; }

std::uint32_t StandaloneAudioEngine::BufferSize() const noexcept { return asio_host_.BufferSize(); }

std::uint32_t StandaloneAudioEngine::InputLatencySamples() const noexcept {
    return asio_host_.InputLatencySamples();
}

std::uint32_t StandaloneAudioEngine::OutputLatencySamples() const noexcept {
    return asio_host_.OutputLatencySamples();
}

bool StandaloneAudioEngine::ResetRequested() const noexcept { return asio_host_.ResetRequested(); }

bool StandaloneAudioEngine::DropoutReported() const noexcept { return asio_host_.DropoutReported(); }

std::uint64_t StandaloneAudioEngine::CallbackCount() const noexcept {
    return callback_count_.load(std::memory_order_relaxed);
}

std::uint32_t StandaloneAudioEngine::LastCallbackSampleCount() const noexcept {
    return last_callback_sample_count_.load(std::memory_order_relaxed);
}

} // namespace solo_rig::standalone
