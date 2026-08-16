#pragma once

#include "core/audio_buffer.hpp"
#include "core/rig_runtime.hpp"
#include "dsp/tuner.hpp"
#include "standalone/asio_device_coordinator.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace solo_rig::standalone {

// Owns the non-real-time Standalone graph and the single-device ASIO host.
// Open/Start/Stop/Close, ReloadPreset and SetNodeBypassed are control-thread operations. The
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
    [[nodiscard]] bool SetNodeBypassed(std::string_view instance_id, bool bypassed);
    [[nodiscard]] bool RecoverAudio();
    void SetTunerEnabled(bool enabled) noexcept;
    [[nodiscard]] bool TunerEnabled() const noexcept;
    // Control/UI-thread operation. The analyzer never runs from an ASIO
    // callback; it reads the lock-free input window captured by ProcessBlock.
    [[nodiscard]] std::optional<TuningResult> AnalyzeTuner() const;
    [[nodiscard]] static bool RunTunerCaptureSelfTest();
    // Control-thread contract test for the production package registration
    // path. It exercises package reload, registry registration and one graph
    // block without opening hardware.
    [[nodiscard]] static bool RunExternalModulePackageRuntimeSelfTest();

    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] const AsioDeviceDescriptor& Device() const noexcept;
    [[nodiscard]] const std::string& StatusText() const noexcept;
    // Control/UI-thread snapshot that appends callback and recovery state.
    // It must not be called from an ASIO callback.
    [[nodiscard]] std::string RuntimeStatusText() const;
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
    [[nodiscard]] bool PrepareRuntimeCandidate(const RigPreset& preset,
                                               const AudioFormat& format,
                                               PreparedPreset& output);
    [[nodiscard]] bool BuildRuntime(const RigPreset& preset, const AudioFormat& format);
    [[nodiscard]] bool QueueRuntimeTransition(RigPreset preset);
    void ReclaimCompletedTransition() noexcept;
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
    std::unique_ptr<PreparedPreset> prepared_runtime_;
    std::unique_ptr<PreparedPreset> queued_runtime_;
    std::optional<RigRuntime> rig_runtime_;
    // Control-thread snapshot used to restore the previous graph when a
    // stopped-boundary preset reload fails validation or preparation.
    std::optional<RigPreset> active_preset_;
    AudioFormat format_{};
    StandaloneAudioRouting routing_{};
    AudioBuffer processing_input_;
    AudioBuffer processing_output_;
    std::string status_text_{"ASIO backend not configured/available"};
    std::string error_text_;
    std::atomic<std::uint64_t> callback_count_{};
    std::atomic<std::uint32_t> last_callback_sample_count_{};
    // At the fixed 192 kHz internal rate this gives the autocorrelator enough
    // periods for low guitar notes without doing analysis on the audio thread.
    static constexpr std::size_t kTunerWindowSamples = 4096U;
    static_assert(std::atomic<float>::is_always_lock_free,
                  "Standalone tuner samples must remain lock-free on the audio boundary");
    std::array<std::atomic<float>, kTunerWindowSamples> tuner_samples_{};
    std::atomic<std::uint64_t> tuner_write_index_{};
    std::atomic<bool> tuner_enabled_{};
};

} // namespace solo_rig::standalone
