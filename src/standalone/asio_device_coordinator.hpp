#pragma once

#include "core/audio_buffer.hpp"
#include "standalone/asio_host.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace solo_rig::standalone {

// The graph callback always runs at the internal 192 kHz rate. Device
// callbacks are converted into and out of this boundary by the coordinator.
using InternalAudioProcessCallback = void (*)(void* user,
                                               const float* const* inputs,
                                               float* const* outputs,
                                               std::uint32_t channel_count,
                                               std::uint32_t sample_count) noexcept;

class AsioDeviceCoordinator final {
public:
    static constexpr std::size_t kMaximumDevices = AsioHost::kMaximumActiveHosts;

    AsioDeviceCoordinator() = default;
    ~AsioDeviceCoordinator();

    AsioDeviceCoordinator(const AsioDeviceCoordinator&) = delete;
    AsioDeviceCoordinator& operator=(const AsioDeviceCoordinator&) = delete;

    // Open prepares one ASIO host per active device in routing. No callback is
    // started until Start(), and all allocations happen on this control path.
    [[nodiscard]] bool Open(std::span<const AsioDriverInfo> drivers,
                            HWND system_window,
                            StandaloneAudioRouting routing);
    void SetProcessCallback(InternalAudioProcessCallback callback, void* user) noexcept;
    [[nodiscard]] bool Start();
    void Stop() noexcept;
    void Close() noexcept;

    // Reopens the saved control-plane configuration after an ASIO reset or a
    // device disconnect. The caller must have stopped the audio thread first.
    [[nodiscard]] bool Recover();
    [[nodiscard]] static bool RunBufferContractSelfTest();
    // Offline contract only: exercises the bounded clock servo with two
    // different device rates. It is not a substitute for two physical ASIO
    // devices, which remains a hardware acceptance gate.
    [[nodiscard]] static bool RunAsyncClockContractSelfTest();

    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] const AsioDeviceDescriptor& MasterDevice() const noexcept;
    [[nodiscard]] std::uint32_t MasterBufferSize() const noexcept;
    [[nodiscard]] std::uint32_t MasterInputLatencySamples() const noexcept;
    [[nodiscard]] std::uint32_t MasterOutputLatencySamples() const noexcept;
    [[nodiscard]] bool ResetRequested() const noexcept;
    [[nodiscard]] bool DropoutReported() const noexcept;
    [[nodiscard]] AsioHostError LastError() const noexcept;
    [[nodiscard]] const std::string& LastErrorMessage() const noexcept;
    [[nodiscard]] std::uint32_t RequiredInternalBlockSize() const noexcept;
    [[nodiscard]] std::size_t ActiveDeviceCount() const noexcept;
    [[nodiscard]] const StandaloneAudioRouting& Routing() const noexcept;

private:
    static constexpr std::size_t kNoEndpoint = kMaximumDevices;

    class AudioFrameRing final {
    public:
        [[nodiscard]] bool Prepare(std::uint32_t channel_count, std::uint32_t capacity_frames);
        void Reset() noexcept;
        [[nodiscard]] bool Write(const float* const* source, std::uint32_t frame_count) noexcept;
        [[nodiscard]] bool Read(float* const* destination, std::uint32_t frame_count) noexcept;
        [[nodiscard]] std::uint32_t AvailableFrames() const noexcept;
        [[nodiscard]] std::uint32_t CapacityFrames() const noexcept;
        [[nodiscard]] bool UnderrunReported() const noexcept;
        [[nodiscard]] bool OverrunReported() const noexcept;

    private:
        std::uint32_t channel_count_{};
        std::uint32_t capacity_frames_{};
        std::vector<float> samples_;
        std::atomic<std::uint64_t> write_frame_{};
        std::atomic<std::uint64_t> read_frame_{};
        std::atomic<bool> underrun_reported_{};
        std::atomic<bool> overrun_reported_{};
    };

    struct Endpoint final {
        AsioDeviceCoordinator* owner{};
        bool active{};
        bool master{};
        std::string device_id;
        AsioHost host;
        double native_rate_hz{};
        std::uint32_t buffer_size{};
        std::uint32_t max_internal_block_size{};
        AudioFrameRing input_ring;
        AudioFrameRing output_ring;
        AudioBuffer input_resample_scratch;
        AudioBuffer input_consume_scratch;
        AudioBuffer output_source_scratch;
        std::vector<const float*> input_resample_pointers;
        std::vector<float*> input_consume_pointers;
        std::vector<float*> output_source_pointers;
        bool has_inputs{};
        bool has_outputs{};
        std::atomic<std::uint64_t> callback_count{};
        std::atomic<std::uint64_t> startup_callbacks{};
    };

    static void DeviceProcessCallback(void* user,
                                      const float* const* inputs,
                                      float* const* outputs,
                                      std::uint32_t channel_count,
                                      std::uint32_t sample_count) noexcept;
    void ProcessDevice(Endpoint& endpoint,
                       const float* const* inputs,
                       float* const* outputs,
                       std::uint32_t channel_count,
                       std::uint32_t sample_count) noexcept;
    void ProcessMaster(Endpoint& endpoint,
                       float* const* outputs,
                       std::uint32_t channel_count,
                       std::uint32_t sample_count) noexcept;
    void ProcessSlaveOutput(Endpoint& endpoint,
                            float* const* outputs,
                            std::uint32_t channel_count,
                            std::uint32_t sample_count) noexcept;
    void ClearDeviceOutputs(float* const* outputs,
                            std::uint32_t channel_count,
                            std::uint32_t sample_count) noexcept;
    [[nodiscard]] bool PrepareEndpoint(Endpoint& endpoint,
                                       const AsioDeviceDescriptor& device,
                                       const StandaloneAudioRouting& routing,
                                       std::uint32_t device_buffer_size,
                                       bool master);
    [[nodiscard]] std::uint32_t DeviceBufferSize(const std::string& device_id,
                                                 std::uint32_t preferred) const noexcept;
    [[nodiscard]] Endpoint* FindEndpoint(std::string_view device_id) noexcept;
    [[nodiscard]] const Endpoint* FindEndpoint(std::string_view device_id) const noexcept;
    [[nodiscard]] static float ClockCorrection(const AudioFrameRing& ring) noexcept;
    void MarkRuntimeFault(Endpoint& endpoint) noexcept;
    void ReleaseEndpoints() noexcept;

    std::array<Endpoint, kMaximumDevices> endpoints_;
    std::vector<AsioDriverInfo> drivers_;
    HWND system_window_{};
    StandaloneAudioRouting routing_{};
    InternalAudioProcessCallback process_callback_{};
    void* process_user_{};
    std::size_t active_device_count_{};
    std::size_t master_endpoint_index_{kMaximumDevices};
    std::uint32_t required_internal_block_size_{};
    std::atomic<bool> reset_requested_{};
    std::atomic<bool> dropout_reported_{};
    AsioHostError last_error_{AsioHostError::None};
    std::string last_error_message_;
    AudioBuffer processing_input_;
    AudioBuffer processing_output_;
    std::vector<const float*> processing_input_pointers_;
    std::vector<float*> processing_output_pointers_;
    std::vector<std::size_t> input_endpoint_by_channel_;
};

} // namespace solo_rig::standalone
