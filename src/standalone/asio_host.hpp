#pragma once

#include "standalone/audio_routing.hpp"
#include "standalone/asio_driver_abi.hpp"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace solo_rig::standalone {

struct AsioDriverInfo {
    std::string driver_id;
    std::string display_name;
    std::string class_id_text;
    std::string dll_path;
    GUID class_id{};
};

struct AsioProbeResult {
    bool succeeded{};
    AsioDeviceDescriptor device;
    std::uint32_t input_latency_samples{};
    std::uint32_t output_latency_samples{};
    std::uint32_t minimum_buffer_size{};
    std::uint32_t maximum_buffer_size{};
    std::uint32_t preferred_buffer_size{};
    std::int32_t buffer_granularity{};
    std::string driver_error;
};

enum class AsioHostError {
    None,
    ComInitializationFailed,
    DriverNotFound,
    DriverCreationFailed,
    DriverInitializationFailed,
    DriverQueryFailed,
    InvalidRouting,
    MultipleDevicesNotSupported,
    UnsupportedSampleRate,
    UnsupportedBufferSize,
    UnsupportedSampleType,
    BufferCreationFailed,
    StartFailed,
};

// The callback is installed before Start() and is invoked only from the ASIO
// real-time callback. It must not allocate, lock, access files, or throw.
using AsioProcessCallback = void (*)(void* user,
                                     const float* const* inputs,
                                     float* const* outputs,
                                     std::uint32_t channel_count,
                                     std::uint32_t sample_count) noexcept;

class AsioHost final {
public:
    static constexpr std::size_t kMaximumActiveHosts = 4U;

    AsioHost() = default;
    ~AsioHost();

    AsioHost(const AsioHost&) = delete;
    AsioHost& operator=(const AsioHost&) = delete;

    [[nodiscard]] static std::vector<AsioDriverInfo> EnumerateDrivers();
    [[nodiscard]] static AsioProbeResult ProbeDriver(const AsioDriverInfo& driver, HWND system_window);
    // Offline callback contract only: verifies that reset and sample-rate
    // notifications are promoted to control-thread recovery state.
    [[nodiscard]] static bool RunRuntimeFaultSelfTest();

    [[nodiscard]] bool Open(const AsioDriverInfo& driver,
                             HWND system_window,
                             const StandaloneAudioRouting& routing,
                             AsioProcessCallback callback,
                             void* callback_user);
    [[nodiscard]] bool Start();
    void Stop() noexcept;
    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] const AsioDeviceDescriptor& Device() const noexcept;
    [[nodiscard]] std::uint32_t BufferSize() const noexcept;
    [[nodiscard]] std::uint32_t InputLatencySamples() const noexcept;
    [[nodiscard]] std::uint32_t OutputLatencySamples() const noexcept;
    [[nodiscard]] AsioHostError LastError() const noexcept;
    [[nodiscard]] const std::string& LastErrorMessage() const noexcept;
    [[nodiscard]] bool ResetRequested() const noexcept;
    [[nodiscard]] bool DropoutReported() const noexcept;
    [[nodiscard]] double SampleRateChangedTo() const noexcept;

private:
    struct BufferBinding {
        bool input{};
        std::uint32_t processing_channel{};
        std::uint32_t buffer_index{};
        asio_abi::ASIOSampleType sample_type{};
        std::size_t scratch_offset{};
    };

    [[nodiscard]] bool OpenDriver(const AsioDriverInfo& driver, HWND system_window);
    [[nodiscard]] bool QueryDriver(AsioProbeResult* probe);
    [[nodiscard]] bool SetError(AsioHostError error, std::string message);
    [[nodiscard]] bool ValidateBufferSize(std::uint32_t requested) const noexcept;
    [[nodiscard]] bool QuerySelectedChannelType(bool input,
                                                std::uint32_t channel,
                                                asio_abi::ASIOSampleType* sample_type);
    void HandleBufferSwitch(long double_buffer_index) noexcept;
    void ClearOutputBuffers() noexcept;
    void ReleaseDriver() noexcept;

    static void BufferSwitch(long double_buffer_index, long direct_process) noexcept;
    static void SampleRateDidChange(double sample_rate) noexcept;
    static long AsioMessage(long selector, long value, void* message, double* optional) noexcept;
    static void BufferSwitch0(long double_buffer_index, long direct_process) noexcept;
    static void BufferSwitch1(long double_buffer_index, long direct_process) noexcept;
    static void BufferSwitch2(long double_buffer_index, long direct_process) noexcept;
    static void BufferSwitch3(long double_buffer_index, long direct_process) noexcept;
    static void SampleRateDidChange0(double sample_rate) noexcept;
    static void SampleRateDidChange1(double sample_rate) noexcept;
    static void SampleRateDidChange2(double sample_rate) noexcept;
    static void SampleRateDidChange3(double sample_rate) noexcept;
    static long AsioMessage0(long selector, long value, void* message, double* optional) noexcept;
    static long AsioMessage1(long selector, long value, void* message, double* optional) noexcept;
    static long AsioMessage2(long selector, long value, void* message, double* optional) noexcept;
    static long AsioMessage3(long selector, long value, void* message, double* optional) noexcept;
    static void BufferSwitchSlot(std::size_t slot, long double_buffer_index, long direct_process) noexcept;
    static void SampleRateDidChangeSlot(std::size_t slot, double sample_rate) noexcept;
    static long AsioMessageSlot(std::size_t slot, long selector, long value, void* message, double* optional) noexcept;
    static AsioHost* ActiveHost(std::size_t slot) noexcept;
    [[nodiscard]] bool AcquireActiveSlot() noexcept;
    void ReleaseActiveSlot() noexcept;

    static constexpr std::size_t kNoActiveSlot = static_cast<std::size_t>(-1);

    AsioDriverInfo driver_info_;
    AsioDeviceDescriptor device_;
    std::string last_error_message_;
    AsioHostError last_error_{AsioHostError::None};
    void* driver_{nullptr};
    bool com_initialized_{};
    bool initialized_{};
    bool buffers_created_{};
    std::atomic<bool> running_{false};
    std::atomic<bool> reset_requested_{false};
    std::atomic<bool> dropout_reported_{false};
    std::atomic<double> sample_rate_changed_to_{0.0};
    std::uint32_t buffer_size_{};
    std::uint32_t input_latency_samples_{};
    std::uint32_t output_latency_samples_{};
    std::uint32_t minimum_buffer_size_{};
    std::uint32_t maximum_buffer_size_{};
    std::uint32_t preferred_buffer_size_{};
    std::int32_t buffer_granularity_{};
    std::vector<asio_abi::ASIOBufferInfo> buffer_infos_;
    std::vector<BufferBinding> input_bindings_;
    std::vector<BufferBinding> output_bindings_;
    std::vector<const float*> input_pointers_;
    std::vector<float*> output_pointers_;
    std::vector<float> zero_buffer_;
    std::vector<float> input_conversion_scratch_;
    std::vector<float> output_conversion_scratch_;
    AsioProcessCallback callback_{};
    void* callback_user_{};
    asio_abi::ASIOCallbacks callbacks_{};
    std::size_t active_slot_{kNoActiveSlot};
};

} // namespace solo_rig::standalone
