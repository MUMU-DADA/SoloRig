#pragma once

#include "core/audio_buffer.hpp"
#include "core/rig_runtime.hpp"
#include "standalone/asio_device_coordinator.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace solo_rig::standalone {

// Owns the non-real-time Standalone graph and the single-device ASIO host.
// Open/Start/Stop/Close and ReloadPreset are control-thread operations. The
// ASIO callback only copies into preallocated buffers and calls RigRuntime.
class StandaloneAudioEngine final {
public:
    StandaloneAudioEngine() = default;
    ~StandaloneAudioEngine();

    StandaloneAudioEngine(const StandaloneAudioEngine&) = delete;
    StandaloneAudioEngine& operator=(const StandaloneAudioEngine&) = delete;

    [[nodiscard]] bool RefreshDrivers();
    [[nodiscard]] const std::vector<AsioDriverInfo>& Drivers() const noexcept;
    [[nodiscard]] bool Open(const AsioDriverInfo& driver, HWND system_window, RigPreset preset);
    [[nodiscard]] bool OpenWithRouting(const AsioDriverInfo& driver,
                                       HWND system_window,
                                       RigPreset preset,
                                       StandaloneAudioRouting routing);
    [[nodiscard]] bool OpenWithDevices(std::vector<AsioDriverInfo> drivers,
                                       HWND system_window,
                                       RigPreset preset,
                                       StandaloneAudioRouting routing);
    [[nodiscard]] bool Start();
    void Stop();
    void Close() noexcept;
    [[nodiscard]] bool ReloadPreset(RigPreset preset);
    [[nodiscard]] bool RecoverAudio();

    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] const AsioDeviceDescriptor& Device() const noexcept;
    [[nodiscard]] const std::string& StatusText() const noexcept;
    [[nodiscard]] const std::string& ErrorText() const noexcept;
    [[nodiscard]] std::uint32_t BufferSize() const noexcept;
    [[nodiscard]] std::uint32_t InputLatencySamples() const noexcept;
    [[nodiscard]] std::uint32_t OutputLatencySamples() const noexcept;
    [[nodiscard]] bool ResetRequested() const noexcept;
    [[nodiscard]] bool DropoutReported() const noexcept;
    [[nodiscard]] std::uint64_t CallbackCount() const noexcept;
    [[nodiscard]] std::uint32_t LastCallbackSampleCount() const noexcept;
    [[nodiscard]] const StandaloneAudioRouting& Routing() const noexcept;

private:
    [[nodiscard]] bool OpenInternal(std::vector<AsioDriverInfo> drivers,
                                    HWND system_window,
                                    RigPreset preset,
                                    StandaloneAudioRouting routing);
    [[nodiscard]] bool BuildRuntime(const RigPreset& preset, const AudioFormat& format);
    void ReleaseRuntime() noexcept;
    void SetStatus(std::string status);
    void SetErrorStatus(std::string prefix);
    static void ProcessCallback(void* user,
                                const float* const* inputs,
                                float* const* outputs,
                                std::uint32_t channel_count,
                                std::uint32_t sample_count) noexcept;
    void ProcessBlock(const float* const* inputs,
                      float* const* outputs,
                      std::uint32_t channel_count,
                      std::uint32_t sample_count) noexcept;

    std::vector<AsioDriverInfo> drivers_;
    AsioDeviceCoordinator asio_coordinator_;
    RigModuleRegistry module_registry_;
    RealtimePresetMailbox mailbox_;
    std::optional<PreparedPreset> prepared_runtime_;
    std::optional<RigRuntime> rig_runtime_;
    AudioFormat format_{};
    StandaloneAudioRouting routing_{};
    AudioBuffer processing_input_;
    AudioBuffer processing_output_;
    std::string status_text_{"ASIO backend not configured/available"};
    std::string error_text_;
    std::atomic<std::uint64_t> callback_count_{};
    std::atomic<std::uint32_t> last_callback_sample_count_{};
};

} // namespace solo_rig::standalone
