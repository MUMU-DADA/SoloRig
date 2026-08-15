#pragma once

#include "core/audio_format.hpp"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace solo_rig {

// The platform backend supplies these descriptors after enumerating ASIO
// drivers. Keeping routing independent from that backend makes it testable
// without an installed interface or proprietary driver SDK.
struct AsioDeviceDescriptor {
    std::string device_id;
    std::string display_name;
    std::uint32_t input_channel_count{};
    std::uint32_t output_channel_count{};
    double native_sample_rate_hz{};
    std::string clock_domain_id;
};

struct PhysicalChannelRoute {
    std::string device_id;
    std::uint32_t device_channel{};
    std::uint32_t processing_channel{};
};

struct AsioDeviceBufferConfig {
    std::string device_id;
    std::uint32_t buffer_size{};
};

struct StandaloneAudioRouting {
    AudioFormat processing_format{.sample_rate_hz = 192000.0,
                                  .max_block_size = 256,
                                  .channel_count = 2,
                                  .sample_representation = SampleRepresentation::Float32};
    // Optional physical-device block size. When zero, the host uses the
    // processing block size for backwards-compatible single-rate routes.
    std::uint32_t device_buffer_size{};
    // Optional per-device overrides used by the multi-ASIO coordinator. When
    // absent, each device's probed preferred size is selected independently.
    std::vector<AsioDeviceBufferConfig> device_buffers;
    std::vector<PhysicalChannelRoute> inputs;
    std::vector<PhysicalChannelRoute> outputs;
};

enum class AudioRoutingError {
    InvalidProcessingFormat,
    InternalSampleRateMustBe192k,
    MissingInputRoute,
    MissingOutputRoute,
    InvalidDeviceDescriptor,
    DuplicateDeviceId,
    UnknownInputDevice,
    UnknownOutputDevice,
    InputChannelOutOfRange,
    OutputChannelOutOfRange,
    ProcessingChannelOutOfRange,
    DuplicateInputPhysicalChannel,
    DuplicateInputProcessingChannel,
    DuplicateOutputPhysicalChannel,
    UnknownDeviceBufferDevice,
    DuplicateDeviceBufferConfig,
    InvalidDeviceBufferSize,
};

struct AudioRoutingAnalysis {
    std::uint32_t active_device_count{};
    std::uint32_t device_rate_conversion_count{};
    bool requires_asynchronous_resampling{};
};

struct AudioRoutingValidationResult {
    std::set<AudioRoutingError> errors;
    AudioRoutingAnalysis analysis;
};

[[nodiscard]] AudioRoutingValidationResult ValidateStandaloneAudioRouting(
    const std::vector<AsioDeviceDescriptor>& devices,
    const StandaloneAudioRouting& routing);

} // namespace solo_rig
