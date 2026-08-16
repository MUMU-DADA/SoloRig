#include "standalone/asio_device_coordinator.hpp"

#include "standalone/audio_resampler.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace solo_rig::standalone {
namespace {

constexpr double kInternalSampleRateHz = 192000.0;
constexpr std::uint32_t kRingBlockCount = 8U;
constexpr float kMaximumClockCorrection = 0.02F;

bool ContainsDevice(const StandaloneAudioRouting& routing, std::string_view device_id) {
    const auto has_id = [device_id](const PhysicalChannelRoute& route) {
        return route.device_id == device_id;
    };
    return std::any_of(routing.inputs.begin(), routing.inputs.end(), has_id) ||
        std::any_of(routing.outputs.begin(), routing.outputs.end(), has_id);
}

std::uint32_t ClampPositiveBlockSize(std::uint32_t value) noexcept {
    return value == 0U ? 1U : value;
}

} // namespace

bool AsioDeviceCoordinator::AudioFrameRing::Prepare(std::uint32_t channel_count,
                                                    std::uint32_t capacity_frames) {
    if (channel_count == 0U || capacity_frames == 0U) {
        return false;
    }
    const auto maximum_samples = std::vector<float>().max_size();
    if (static_cast<std::size_t>(capacity_frames) > maximum_samples / channel_count) {
        return false;
    }
    const auto sample_count = static_cast<std::size_t>(channel_count) * capacity_frames;
    channel_count_ = channel_count;
    capacity_frames_ = capacity_frames;
    samples_.assign(sample_count, 0.0F);
    Reset();
    return true;
}

void AsioDeviceCoordinator::AudioFrameRing::Reset() noexcept {
    write_frame_.store(0U, std::memory_order_release);
    read_frame_.store(0U, std::memory_order_release);
    underrun_reported_.store(false, std::memory_order_release);
    overrun_reported_.store(false, std::memory_order_release);
}

bool AsioDeviceCoordinator::AudioFrameRing::Write(const float* const* source,
                                                  std::uint32_t frame_count) noexcept {
    if (frame_count == 0U) {
        return true;
    }
    if (source == nullptr || channel_count_ == 0U || capacity_frames_ == 0U ||
        frame_count > capacity_frames_) {
        overrun_reported_.store(true, std::memory_order_release);
        return false;
    }
    const auto write = write_frame_.load(std::memory_order_relaxed);
    const auto read = read_frame_.load(std::memory_order_acquire);
    const auto available = write >= read ? write - read : 0U;
    const auto free_frames = available >= capacity_frames_ ? 0U : capacity_frames_ - available;
    if (frame_count > free_frames) {
        overrun_reported_.store(true, std::memory_order_release);
        return false;
    }
    for (std::uint32_t channel = 0U; channel < channel_count_; ++channel) {
        auto* destination = samples_.data() + static_cast<std::size_t>(channel) * capacity_frames_;
        for (std::uint32_t frame = 0U; frame < frame_count; ++frame) {
            const auto ring_index = static_cast<std::size_t>((write + frame) % capacity_frames_);
            destination[ring_index] = source[channel] == nullptr ? 0.0F : source[channel][frame];
        }
    }
    write_frame_.store(write + frame_count, std::memory_order_release);
    return true;
}

bool AsioDeviceCoordinator::AudioFrameRing::Read(float* const* destination,
                                                 std::uint32_t frame_count) noexcept {
    if (frame_count == 0U) {
        return true;
    }
    if (destination == nullptr || channel_count_ == 0U || capacity_frames_ == 0U ||
        frame_count > capacity_frames_) {
        underrun_reported_.store(true, std::memory_order_release);
        return false;
    }
    const auto read = read_frame_.load(std::memory_order_relaxed);
    const auto write = write_frame_.load(std::memory_order_acquire);
    const auto available = write >= read ? write - read : 0U;
    const auto readable = static_cast<std::uint32_t>(std::min<std::uint64_t>(available, frame_count));
    if (readable < frame_count) {
        underrun_reported_.store(true, std::memory_order_release);
    }
    for (std::uint32_t channel = 0U; channel < channel_count_; ++channel) {
        const auto* source = samples_.data() + static_cast<std::size_t>(channel) * capacity_frames_;
        if (destination[channel] == nullptr) {
            continue;
        }
        for (std::uint32_t frame = 0U; frame < readable; ++frame) {
            const auto ring_index = static_cast<std::size_t>((read + frame) % capacity_frames_);
            destination[channel][frame] = source[ring_index];
        }
        std::fill(destination[channel] + readable,
                  destination[channel] + frame_count,
                  0.0F);
    }
    // Do not advance beyond the producer when an asynchronous device has not
    // delivered a full block yet; otherwise the unsigned SPSC indices would
    // remain permanently inverted and every later block would underflow.
    read_frame_.store(read + readable, std::memory_order_release);
    return readable == frame_count;
}

std::uint32_t AsioDeviceCoordinator::AudioFrameRing::AvailableFrames() const noexcept {
    const auto write = write_frame_.load(std::memory_order_acquire);
    const auto read = read_frame_.load(std::memory_order_acquire);
    const auto available = write >= read ? write - read : 0U;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(available, capacity_frames_));
}

std::uint32_t AsioDeviceCoordinator::AudioFrameRing::CapacityFrames() const noexcept {
    return capacity_frames_;
}

bool AsioDeviceCoordinator::AudioFrameRing::UnderrunReported() const noexcept {
    return underrun_reported_.load(std::memory_order_acquire);
}

bool AsioDeviceCoordinator::AudioFrameRing::OverrunReported() const noexcept {
    return overrun_reported_.load(std::memory_order_acquire);
}

AsioDeviceCoordinator::~AsioDeviceCoordinator() {
    Close();
}

bool AsioDeviceCoordinator::RunBufferContractSelfTest() {
    AudioFrameRing ring;
    if (!ring.Prepare(2U, 8U)) {
        return false;
    }
    const float input_left[] = {1.0F, 2.0F, 3.0F};
    const float input_right[] = {10.0F, 20.0F, 30.0F};
    const float* input[] = {input_left, input_right};
    if (!ring.Write(input, 3U)) {
        return false;
    }
    float first_left[2]{};
    float first_right[2]{};
    float* first[] = {first_left, first_right};
    if (!ring.Read(first, 2U) || first_left[0] != 1.0F || first_left[1] != 2.0F ||
        first_right[0] != 10.0F || first_right[1] != 20.0F) {
        return false;
    }
    float underflow_left[3]{};
    float underflow_right[3]{};
    float* underflow[] = {underflow_left, underflow_right};
    if (ring.Read(underflow, 3U) || underflow_left[0] != 3.0F || underflow_left[1] != 0.0F ||
        underflow_left[2] != 0.0F || !ring.UnderrunReported()) {
        return false;
    }
    if (!ring.Write(input, 3U)) {
        return false;
    }
    float recovered_left[3]{};
    float recovered_right[3]{};
    float* recovered[] = {recovered_left, recovered_right};
    if (!ring.Read(recovered, 3U) || recovered_left[0] != 1.0F || recovered_left[1] != 2.0F ||
        recovered_left[2] != 3.0F || recovered_right[0] != 10.0F || recovered_right[2] != 30.0F) {
        return false;
    }
    ring.Reset();
    if (!ring.Write(input, 3U)) {
        return false;
    }
    return ring.AvailableFrames() == 3U;
}

bool AsioDeviceCoordinator::RunAsyncClockContractSelfTest() {
    constexpr std::uint32_t kDeviceBlockSize = 64U;
    constexpr double kMasterRateHz = 48000.0;
    constexpr double kSlaveRateHz = 44100.0;
    constexpr std::uint32_t kInternalBlockSize = 256U;
    constexpr std::uint32_t kRingCapacity = kInternalBlockSize * kRingBlockCount;
    if (kInternalBlockSize == 0U || kRingCapacity == 0U) {
        return false;
    }

    AudioFrameRing ring;
    if (!ring.Prepare(1U, kRingCapacity)) {
        return false;
    }
    std::array<float, kInternalBlockSize> produced{};
    const float* produced_channels[] = {produced.data()};
    for (std::uint32_t block = 0U; block < kRingBlockCount / 2U; ++block) {
        if (!ring.Write(produced_channels, kInternalBlockSize)) {
            return false;
        }
    }

    std::array<float, 320U> consumed{};
    float* consumed_channels[] = {consumed.data()};
    double next_master_callback = 0.0;
    double next_slave_callback = 0.0;
    for (std::size_t callback = 0U; callback < 30000U; ++callback) {
        if (next_master_callback <= next_slave_callback) {
            if (!ring.Write(produced_channels, kInternalBlockSize)) {
                return false;
            }
            next_master_callback += static_cast<double>(kDeviceBlockSize) / kMasterRateHz;
            continue;
        }
        const auto correction = ClockCorrection(ring);
        const auto source_rate = kInternalSampleRateHz * (1.0 + static_cast<double>(correction));
        const auto source_sample_count = ResampledBlockSize(kDeviceBlockSize, kSlaveRateHz, source_rate);
        if (source_sample_count == 0U || source_sample_count > consumed.size() ||
            !ring.Read(consumed_channels, source_sample_count)) {
            return false;
        }
        next_slave_callback += static_cast<double>(kDeviceBlockSize) / kSlaveRateHz;
    }
    return !ring.UnderrunReported() && !ring.OverrunReported() &&
        ring.AvailableFrames() > 0U && ring.AvailableFrames() < ring.CapacityFrames();
}

bool AsioDeviceCoordinator::Open(std::span<const AsioDriverInfo> drivers,
                                 HWND system_window,
                                 StandaloneAudioRouting routing) {
    Close();
    last_error_ = AsioHostError::None;
    last_error_message_.clear();
    if (drivers.empty()) {
        last_error_ = AsioHostError::DriverNotFound;
        last_error_message_ = "no ASIO drivers were supplied for the requested routing";
        return false;
    }
    try {
        std::vector<AsioDeviceDescriptor> devices;
        std::vector<AsioProbeResult> probes;
        devices.reserve(kMaximumDevices);
        probes.reserve(kMaximumDevices);
        drivers_.assign(drivers.begin(), drivers.end());
        system_window_ = system_window != nullptr ? system_window : GetDesktopWindow();

        for (const auto& driver : drivers_) {
            if (!ContainsDevice(routing, driver.driver_id)) {
                continue;
            }
            auto probe = AsioHost::ProbeDriver(driver, system_window_);
            if (!probe.succeeded) {
                last_error_ = AsioHostError::DriverQueryFailed;
                last_error_message_ = probe.driver_error;
                Close();
                return false;
            }
            devices.push_back(probe.device);
            probes.push_back(std::move(probe));
        }
        if (devices.empty()) {
            last_error_ = AsioHostError::DriverNotFound;
            last_error_message_ = "no supplied ASIO driver matches the requested physical routes";
            Close();
            return false;
        }
        const auto validation = ValidateStandaloneAudioRouting(devices, routing);
        if (!validation.errors.empty()) {
            last_error_ = validation.analysis.active_device_count > kMaximumDevices
                ? AsioHostError::MultipleDevicesNotSupported
                : AsioHostError::InvalidRouting;
            last_error_message_ = validation.analysis.active_device_count > kMaximumDevices
                ? "the ASIO coordinator supports at most four active devices"
                : "multi-device ASIO route validation rejected the requested map";
            Close();
            return false;
        }
        if (validation.analysis.active_device_count > kMaximumDevices) {
            last_error_ = AsioHostError::MultipleDevicesNotSupported;
            last_error_message_ = "the ASIO coordinator supports at most four active devices";
            Close();
            return false;
        }
        if (routing.processing_format.sample_rate_hz <= 0.0 ||
            std::fabs(routing.processing_format.sample_rate_hz - kInternalSampleRateHz) > 0.001 ||
            routing.processing_format.channel_count == 0U) {
            last_error_ = AsioHostError::InvalidRouting;
            last_error_message_ = "the ASIO coordinator requires a 192 kHz processing format";
            Close();
            return false;
        }

        std::string master_id;
        for (const auto& route : routing.outputs) {
            if (!route.device_id.empty()) {
                master_id = route.device_id;
                break;
            }
        }
        if (master_id.empty()) {
            last_error_ = AsioHostError::InvalidRouting;
            last_error_message_ = "multi-device ASIO routing requires an output route";
            Close();
            return false;
        }

        routing_ = routing;
        required_internal_block_size_ = ClampPositiveBlockSize(routing.processing_format.max_block_size);
        for (const auto& probe : probes) {
            const auto buffer_size = DeviceBufferSize(probe.device.device_id, probe.preferred_buffer_size);
        const auto internal_block = ResampledBlockSize(buffer_size,
                                                            probe.device.native_sample_rate_hz,
                                                            routing.processing_format.sample_rate_hz);
            if (internal_block == 0U || internal_block > std::numeric_limits<std::uint32_t>::max() - 2U) {
                last_error_ = AsioHostError::InvalidRouting;
                last_error_message_ = "ASIO route contains an invalid device rate or block size";
                Close();
                return false;
            }
            required_internal_block_size_ = std::max(required_internal_block_size_, internal_block + 2U);
        }
        routing.processing_format.max_block_size = required_internal_block_size_;
        routing_ = routing;
        processing_input_ = AudioBuffer(routing.processing_format.channel_count,
                                        required_internal_block_size_);
        processing_output_ = AudioBuffer(routing.processing_format.channel_count,
                                         required_internal_block_size_);
        processing_input_pointers_.resize(routing.processing_format.channel_count);
        processing_output_pointers_.resize(routing.processing_format.channel_count);
        input_endpoint_by_channel_.assign(routing.processing_format.channel_count, kNoEndpoint);

        std::size_t endpoint_index = 0U;
        for (const auto& probe : probes) {
            if (endpoint_index >= kMaximumDevices) {
                last_error_ = AsioHostError::MultipleDevicesNotSupported;
                last_error_message_ = "the ASIO coordinator supports at most four active devices";
                Close();
                return false;
            }
            auto& endpoint = endpoints_[endpoint_index];
            const auto is_master = probe.device.device_id == master_id;
            const auto buffer_size = DeviceBufferSize(probe.device.device_id, probe.preferred_buffer_size);
            if (!PrepareEndpoint(endpoint,
                                 probe.device,
                                 routing,
                                 buffer_size,
                                 is_master)) {
                last_error_ = AsioHostError::DriverQueryFailed;
                last_error_message_ = "ASIO coordinator could not allocate fixed callback buffers";
                Close();
                return false;
            }
            StandaloneAudioRouting device_routing = routing;
            device_routing.device_buffer_size = buffer_size;
            device_routing.device_buffers.clear();
            device_routing.inputs.clear();
            device_routing.outputs.clear();
            for (const auto& route : routing.inputs) {
                if (route.device_id == probe.device.device_id) {
                    device_routing.inputs.push_back(route);
                }
            }
            for (const auto& route : routing.outputs) {
                if (route.device_id == probe.device.device_id) {
                    device_routing.outputs.push_back(route);
                }
            }
            if (!endpoint.host.Open(*std::find_if(drivers_.begin(), drivers_.end(), [&probe](const auto& driver) {
                    return driver.driver_id == probe.device.device_id;
                }),
                system_window_,
                device_routing,
                &AsioDeviceCoordinator::DeviceProcessCallback,
                &endpoint)) {
                last_error_ = endpoint.host.LastError();
                last_error_message_ = endpoint.host.LastErrorMessage();
                Close();
                return false;
            }
            endpoint_index++;
        }
        active_device_count_ = endpoint_index;
        for (std::size_t index = 0U; index < active_device_count_; ++index) {
            if (endpoints_[index].master) {
                master_endpoint_index_ = index;
                break;
            }
        }
        if (master_endpoint_index_ == kMaximumDevices) {
            last_error_ = AsioHostError::InvalidRouting;
            last_error_message_ = "ASIO coordinator could not select an output master device";
            Close();
            return false;
        }
        for (const auto& route : routing.inputs) {
            const auto* endpoint = FindEndpoint(route.device_id);
            if (endpoint == nullptr || route.processing_channel >= input_endpoint_by_channel_.size()) {
                last_error_ = AsioHostError::InvalidRouting;
                last_error_message_ = "ASIO coordinator could not map an input route to a device";
                Close();
                return false;
            }
            const auto endpoint_index_value = static_cast<std::size_t>(endpoint - endpoints_.data());
            input_endpoint_by_channel_[route.processing_channel] = endpoint_index_value;
        }
        reset_requested_.store(false, std::memory_order_release);
        dropout_reported_.store(false, std::memory_order_release);
        last_error_ = AsioHostError::None;
        last_error_message_.clear();
        return true;
    } catch (...) {
        last_error_ = AsioHostError::DriverQueryFailed;
        last_error_message_ = "ASIO coordinator preparation ran out of memory";
        Close();
        return false;
    }
}

void AsioDeviceCoordinator::SetProcessCallback(InternalAudioProcessCallback callback, void* user) noexcept {
    process_callback_ = callback;
    process_user_ = user;
}

bool AsioDeviceCoordinator::Start() {
    if (!IsOpen() || process_callback_ == nullptr) {
        last_error_ = AsioHostError::StartFailed;
        last_error_message_ = "ASIO coordinator is not prepared with a graph callback";
        return false;
    }
    for (std::size_t index = 0U; index < active_device_count_; ++index) {
        if (!endpoints_[index].host.Start()) {
            last_error_ = endpoints_[index].host.LastError();
            last_error_message_ = endpoints_[index].host.LastErrorMessage();
            for (std::size_t stop_index = 0U; stop_index < index; ++stop_index) {
                endpoints_[stop_index].host.Stop();
            }
            return false;
        }
    }
    return true;
}

void AsioDeviceCoordinator::Stop() noexcept {
    for (std::size_t index = active_device_count_; index > 0U; --index) {
        endpoints_[index - 1U].host.Stop();
    }
}

void AsioDeviceCoordinator::Close() noexcept {
    Stop();
    ReleaseEndpoints();
    drivers_.clear();
    system_window_ = nullptr;
    routing_ = {};
    process_callback_ = nullptr;
    process_user_ = nullptr;
    active_device_count_ = 0U;
    master_endpoint_index_ = kMaximumDevices;
    required_internal_block_size_ = 0U;
    processing_input_ = {};
    processing_output_ = {};
    processing_input_pointers_.clear();
    processing_output_pointers_.clear();
    input_endpoint_by_channel_.clear();
    reset_requested_.store(false, std::memory_order_release);
    dropout_reported_.store(false, std::memory_order_release);
}

bool AsioDeviceCoordinator::Recover() {
    if (drivers_.empty() || routing_.inputs.empty() || routing_.outputs.empty()) {
        last_error_ = AsioHostError::InvalidRouting;
        last_error_message_ = "ASIO coordinator has no saved route for recovery";
        return false;
    }
    const auto saved_drivers = drivers_;
    const auto saved_routing = routing_;
    const auto saved_window = system_window_;
    const auto saved_callback = process_callback_;
    const auto saved_user = process_user_;
    Stop();
    if (!Open(saved_drivers, saved_window, saved_routing)) {
        return false;
    }
    SetProcessCallback(saved_callback, saved_user);
    return Start();
}

bool AsioDeviceCoordinator::IsOpen() const noexcept {
    if (active_device_count_ == 0U) {
        return false;
    }
    for (std::size_t index = 0U; index < active_device_count_; ++index) {
        if (!endpoints_[index].host.IsOpen()) {
            return false;
        }
    }
    return true;
}

bool AsioDeviceCoordinator::IsRunning() const noexcept {
    if (active_device_count_ == 0U) {
        return false;
    }
    for (std::size_t index = 0U; index < active_device_count_; ++index) {
        if (!endpoints_[index].host.IsRunning()) {
            return false;
        }
    }
    return true;
}

const AsioDeviceDescriptor& AsioDeviceCoordinator::MasterDevice() const noexcept {
    static const AsioDeviceDescriptor empty;
    return master_endpoint_index_ < active_device_count_
        ? endpoints_[master_endpoint_index_].host.Device()
        : empty;
}

std::uint32_t AsioDeviceCoordinator::MasterBufferSize() const noexcept {
    return master_endpoint_index_ < active_device_count_ ? endpoints_[master_endpoint_index_].host.BufferSize() : 0U;
}

std::uint32_t AsioDeviceCoordinator::MasterInputLatencySamples() const noexcept {
    return master_endpoint_index_ < active_device_count_
        ? endpoints_[master_endpoint_index_].host.InputLatencySamples()
        : 0U;
}

std::uint32_t AsioDeviceCoordinator::MasterOutputLatencySamples() const noexcept {
    return master_endpoint_index_ < active_device_count_
        ? endpoints_[master_endpoint_index_].host.OutputLatencySamples()
        : 0U;
}

bool AsioDeviceCoordinator::ResetRequested() const noexcept {
    if (reset_requested_.load(std::memory_order_acquire)) {
        return true;
    }
    for (std::size_t index = 0U; index < active_device_count_; ++index) {
        if (endpoints_[index].host.ResetRequested()) {
            return true;
        }
    }
    return false;
}

bool AsioDeviceCoordinator::DropoutReported() const noexcept {
    if (dropout_reported_.load(std::memory_order_acquire)) {
        return true;
    }
    for (std::size_t index = 0U; index < active_device_count_; ++index) {
        const auto& endpoint = endpoints_[index];
        const auto warmup = endpoint.startup_callbacks.load(std::memory_order_acquire) <= 2U;
        if (endpoint.host.DropoutReported() || endpoint.input_ring.OverrunReported() ||
            endpoint.output_ring.OverrunReported() ||
            (!warmup && (endpoint.input_ring.UnderrunReported() || endpoint.output_ring.UnderrunReported()))) {
            return true;
        }
    }
    return false;
}

double AsioDeviceCoordinator::SampleRateChangedTo() const noexcept {
    for (std::size_t index = 0U; index < active_device_count_; ++index) {
        const auto sample_rate = endpoints_[index].host.SampleRateChangedTo();
        if (std::isfinite(sample_rate) && sample_rate > 0.0) {
            return sample_rate;
        }
    }
    return 0.0;
}

AsioHostError AsioDeviceCoordinator::LastError() const noexcept {
    return last_error_;
}

const std::string& AsioDeviceCoordinator::LastErrorMessage() const noexcept {
    return last_error_message_;
}

std::uint32_t AsioDeviceCoordinator::RequiredInternalBlockSize() const noexcept {
    return required_internal_block_size_;
}

std::size_t AsioDeviceCoordinator::ActiveDeviceCount() const noexcept {
    return active_device_count_;
}

const StandaloneAudioRouting& AsioDeviceCoordinator::Routing() const noexcept {
    return routing_;
}

void AsioDeviceCoordinator::DeviceProcessCallback(void* user,
                                                  const float* const* inputs,
                                                  float* const* outputs,
                                                  std::uint32_t channel_count,
                                                  std::uint32_t sample_count) noexcept {
    if (user == nullptr) {
        return;
    }
    auto* endpoint = static_cast<Endpoint*>(user);
    // Endpoint lifetime is controlled by Stop/Close on the control thread;
    // ASIO guarantees no callback after stop returns.
    if (endpoint->owner != nullptr) {
        endpoint->owner->ProcessDevice(*endpoint, inputs, outputs, channel_count, sample_count);
        endpoint->owner->MarkRuntimeFault(*endpoint);
    }
}

void AsioDeviceCoordinator::ProcessDevice(Endpoint& endpoint,
                                          const float* const* inputs,
                                          float* const* outputs,
                                          std::uint32_t channel_count,
                                          std::uint32_t sample_count) noexcept {
    endpoint.callback_count.fetch_add(1U, std::memory_order_relaxed);
    endpoint.startup_callbacks.fetch_add(1U, std::memory_order_relaxed);
    if (channel_count != routing_.processing_format.channel_count || sample_count == 0U ||
        sample_count > endpoint.buffer_size) {
        ClearDeviceOutputs(outputs, channel_count, sample_count);
        dropout_reported_.store(true, std::memory_order_release);
        return;
    }
    const auto correction = ClockCorrection(endpoint.input_ring);
    const auto adjusted_destination_rate = kInternalSampleRateHz * (1.0 - static_cast<double>(correction));
    const auto internal_sample_count = ResampledBlockSize(sample_count,
                                                           endpoint.native_rate_hz,
                                                           adjusted_destination_rate);
    if (internal_sample_count == 0U || internal_sample_count > endpoint.max_internal_block_size) {
        ClearDeviceOutputs(outputs, channel_count, sample_count);
        dropout_reported_.store(true, std::memory_order_release);
        return;
    }
    for (std::uint32_t channel = 0U; channel < channel_count; ++channel) {
        const auto* source = inputs == nullptr ? nullptr : inputs[channel];
        auto* destination = endpoint.input_resample_scratch.ChannelData(channel);
        if (source == nullptr) {
            std::fill_n(destination, internal_sample_count, 0.0F);
        } else if (!ResampleLinear(std::span<const float>(source, sample_count),
                                   std::span<float>(destination, internal_sample_count),
                                   endpoint.native_rate_hz,
                                   adjusted_destination_rate)) {
            std::fill_n(destination, internal_sample_count, 0.0F);
            dropout_reported_.store(true, std::memory_order_release);
        }
        endpoint.input_resample_pointers[channel] = destination;
    }
    if (!endpoint.input_ring.Write(endpoint.input_resample_pointers.data(), internal_sample_count)) {
        dropout_reported_.store(true, std::memory_order_release);
    }
    if (endpoint.master) {
        ProcessMaster(endpoint, outputs, channel_count, sample_count);
    } else {
        ProcessSlaveOutput(endpoint, outputs, channel_count, sample_count);
    }
}

void AsioDeviceCoordinator::ProcessMaster(Endpoint& endpoint,
                                          float* const* outputs,
                                          std::uint32_t channel_count,
                                          std::uint32_t sample_count) noexcept {
    const auto internal_sample_count = ResampledBlockSize(sample_count,
                                                           endpoint.native_rate_hz,
                                                           kInternalSampleRateHz);
    if (internal_sample_count == 0U || internal_sample_count > required_internal_block_size_) {
        ClearDeviceOutputs(outputs, channel_count, sample_count);
        dropout_reported_.store(true, std::memory_order_release);
        return;
    }
    processing_input_.Clear(internal_sample_count);
    for (std::size_t index = 0U; index < active_device_count_; ++index) {
        auto& input_endpoint = endpoints_[index];
        if (!input_endpoint.has_inputs) {
            continue;
        }
        for (std::uint32_t channel = 0U; channel < channel_count; ++channel) {
            input_endpoint.input_consume_pointers[channel] =
                input_endpoint.input_consume_scratch.ChannelData(channel);
        }
        if (!input_endpoint.input_ring.Read(input_endpoint.input_consume_pointers.data(),
                                            internal_sample_count)) {
            const auto master_warmup = endpoints_[master_endpoint_index_].startup_callbacks.load(
                std::memory_order_relaxed) <= 2U;
            if (!master_warmup) {
                dropout_reported_.store(true, std::memory_order_release);
            }
        }
        for (std::uint32_t channel = 0U; channel < channel_count; ++channel) {
            if (channel >= input_endpoint_by_channel_.size() ||
                input_endpoint_by_channel_[channel] != index) {
                continue;
            }
            std::copy_n(input_endpoint.input_consume_scratch.ChannelData(channel),
                        internal_sample_count,
                        processing_input_.ChannelData(channel));
        }
    }
    for (std::uint32_t channel = 0U; channel < channel_count; ++channel) {
        processing_input_pointers_[channel] = processing_input_.ChannelData(channel);
        processing_output_pointers_[channel] = processing_output_.ChannelData(channel);
    }
    if (process_callback_ != nullptr) {
        process_callback_(process_user_,
                          processing_input_pointers_.data(),
                          processing_output_pointers_.data(),
                          channel_count,
                          internal_sample_count);
    } else {
        processing_output_.Clear(internal_sample_count);
    }
    for (std::size_t index = 0U; index < active_device_count_; ++index) {
        auto& output_endpoint = endpoints_[index];
        if (output_endpoint.master || !output_endpoint.has_outputs) {
            continue;
        }
        if (!output_endpoint.output_ring.Write(processing_output_pointers_.data(), internal_sample_count)) {
            dropout_reported_.store(true, std::memory_order_release);
        }
    }
    for (std::uint32_t channel = 0U; channel < channel_count; ++channel) {
        auto* destination = outputs == nullptr ? nullptr : outputs[channel];
        if (destination == nullptr) {
            continue;
        }
        if (!ResampleLinear(std::span<const float>(processing_output_.ChannelData(channel), internal_sample_count),
                            std::span<float>(destination, sample_count),
                            kInternalSampleRateHz,
                            endpoint.native_rate_hz)) {
            std::fill_n(destination, sample_count, 0.0F);
            dropout_reported_.store(true, std::memory_order_release);
        }
    }
}

void AsioDeviceCoordinator::ProcessSlaveOutput(Endpoint& endpoint,
                                               float* const* outputs,
                                               std::uint32_t channel_count,
                                               std::uint32_t sample_count) noexcept {
    if (!endpoint.has_outputs) {
        ClearDeviceOutputs(outputs, channel_count, sample_count);
        return;
    }
    const auto correction = ClockCorrection(endpoint.output_ring);
    const auto adjusted_source_rate = kInternalSampleRateHz * (1.0 + static_cast<double>(correction));
    const auto source_sample_count = ResampledBlockSize(sample_count,
                                                        endpoint.native_rate_hz,
                                                        adjusted_source_rate);
    if (source_sample_count == 0U || source_sample_count > endpoint.max_internal_block_size) {
        ClearDeviceOutputs(outputs, channel_count, sample_count);
        dropout_reported_.store(true, std::memory_order_release);
        return;
    }
    for (std::uint32_t channel = 0U; channel < channel_count; ++channel) {
        endpoint.output_source_pointers[channel] = endpoint.output_source_scratch.ChannelData(channel);
    }
    const auto had_full_block = endpoint.output_ring.Read(endpoint.output_source_pointers.data(), source_sample_count);
    if (!had_full_block &&
        endpoints_[master_endpoint_index_].startup_callbacks.load(std::memory_order_relaxed) > 2U) {
        dropout_reported_.store(true, std::memory_order_release);
    }
    for (std::uint32_t channel = 0U; channel < channel_count; ++channel) {
        auto* destination = outputs == nullptr ? nullptr : outputs[channel];
        if (destination == nullptr) {
            continue;
        }
        if (!ResampleLinear(std::span<const float>(endpoint.output_source_scratch.ChannelData(channel), source_sample_count),
                            std::span<float>(destination, sample_count),
                            adjusted_source_rate,
                            endpoint.native_rate_hz)) {
            std::fill_n(destination, sample_count, 0.0F);
            dropout_reported_.store(true, std::memory_order_release);
        }
    }
}

void AsioDeviceCoordinator::ClearDeviceOutputs(float* const* outputs,
                                                std::uint32_t channel_count,
                                                std::uint32_t sample_count) noexcept {
    if (outputs == nullptr) {
        return;
    }
    for (std::uint32_t channel = 0U; channel < channel_count; ++channel) {
        if (outputs[channel] != nullptr) {
            std::fill_n(outputs[channel], sample_count, 0.0F);
        }
    }
}

bool AsioDeviceCoordinator::PrepareEndpoint(Endpoint& endpoint,
                                             const AsioDeviceDescriptor& device,
                                             const StandaloneAudioRouting& routing,
                                             std::uint32_t device_buffer_size,
                                             bool master) {
    endpoint.owner = this;
    endpoint.active = true;
    endpoint.master = master;
    endpoint.device_id = device.device_id;
    endpoint.native_rate_hz = device.native_sample_rate_hz;
    endpoint.buffer_size = device_buffer_size;
    endpoint.max_internal_block_size = required_internal_block_size_;
    endpoint.has_inputs = std::any_of(routing.inputs.begin(), routing.inputs.end(), [&device](const auto& route) {
        return route.device_id == device.device_id;
    });
    endpoint.has_outputs = std::any_of(routing.outputs.begin(), routing.outputs.end(), [&device](const auto& route) {
        return route.device_id == device.device_id;
    });
    const auto channel_count = routing.processing_format.channel_count;
    if (required_internal_block_size_ > std::numeric_limits<std::uint32_t>::max() / kRingBlockCount) {
        return false;
    }
    const auto ring_capacity = required_internal_block_size_ * kRingBlockCount;
    if (ring_capacity < required_internal_block_size_ ||
        !endpoint.input_ring.Prepare(channel_count, ring_capacity) ||
        !endpoint.output_ring.Prepare(channel_count, ring_capacity)) {
        return false;
    }
    endpoint.input_resample_scratch = AudioBuffer(channel_count, required_internal_block_size_);
    endpoint.input_consume_scratch = AudioBuffer(channel_count, required_internal_block_size_);
    endpoint.output_source_scratch = AudioBuffer(channel_count, required_internal_block_size_);
    if (!endpoint.input_resample_scratch.IsValid() || !endpoint.input_consume_scratch.IsValid() ||
        !endpoint.output_source_scratch.IsValid()) {
        return false;
    }
    endpoint.input_resample_pointers.resize(channel_count);
    endpoint.input_consume_pointers.resize(channel_count);
    endpoint.output_source_pointers.resize(channel_count);
    for (std::uint32_t channel = 0U; channel < channel_count; ++channel) {
        endpoint.input_resample_pointers[channel] = endpoint.input_resample_scratch.ChannelData(channel);
        endpoint.input_consume_pointers[channel] = endpoint.input_consume_scratch.ChannelData(channel);
        endpoint.output_source_pointers[channel] = endpoint.output_source_scratch.ChannelData(channel);
    }
    endpoint.callback_count.store(0U, std::memory_order_release);
    endpoint.startup_callbacks.store(0U, std::memory_order_release);
    return true;
}

std::uint32_t AsioDeviceCoordinator::DeviceBufferSize(const std::string& device_id,
                                                      std::uint32_t preferred) const noexcept {
    for (const auto& config : routing_.device_buffers) {
        if (config.device_id == device_id && config.buffer_size != 0U) {
            return config.buffer_size;
        }
    }
    if (routing_.device_buffer_size != 0U) {
        return routing_.device_buffer_size;
    }
    return preferred;
}

AsioDeviceCoordinator::Endpoint* AsioDeviceCoordinator::FindEndpoint(std::string_view device_id) noexcept {
    for (std::size_t index = 0U; index < active_device_count_; ++index) {
        if (endpoints_[index].device_id == device_id) {
            return &endpoints_[index];
        }
    }
    return nullptr;
}

const AsioDeviceCoordinator::Endpoint* AsioDeviceCoordinator::FindEndpoint(std::string_view device_id) const noexcept {
    for (std::size_t index = 0U; index < active_device_count_; ++index) {
        if (endpoints_[index].device_id == device_id) {
            return &endpoints_[index];
        }
    }
    return nullptr;
}

float AsioDeviceCoordinator::ClockCorrection(const AudioFrameRing& ring) noexcept {
    const auto capacity = ring.CapacityFrames();
    const auto available = ring.AvailableFrames();
    if (capacity == 0U || available == 0U) {
        return 0.0F;
    }
    // The ring target is half full. The bounded correction is a small clock
    // servo: it absorbs independent device-rate drift without allowing a
    // runaway resampling ratio or an unbounded real-time queue.
    constexpr float target_fraction = 0.5F;
    const auto normalized_error = static_cast<float>(available) /
        static_cast<float>(capacity) - target_fraction;
    return std::clamp(normalized_error * 0.04F,
                      -kMaximumClockCorrection,
                      kMaximumClockCorrection);
}

void AsioDeviceCoordinator::MarkRuntimeFault(Endpoint& endpoint) noexcept {
    if (endpoint.host.ResetRequested()) {
        reset_requested_.store(true, std::memory_order_release);
    }
    if (endpoint.host.DropoutReported()) {
        dropout_reported_.store(true, std::memory_order_release);
    }
}

void AsioDeviceCoordinator::ReleaseEndpoints() noexcept {
    for (auto& endpoint : endpoints_) {
        endpoint.host.Close();
        endpoint.owner = nullptr;
        endpoint.active = false;
        endpoint.master = false;
        endpoint.device_id.clear();
        endpoint.input_resample_pointers.clear();
        endpoint.input_consume_pointers.clear();
        endpoint.output_source_pointers.clear();
        endpoint.input_resample_scratch = {};
        endpoint.input_consume_scratch = {};
        endpoint.output_source_scratch = {};
        endpoint.input_ring.Reset();
        endpoint.output_ring.Reset();
        endpoint.callback_count.store(0U, std::memory_order_release);
        endpoint.startup_callbacks.store(0U, std::memory_order_release);
    }
}

} // namespace solo_rig::standalone
