#pragma once

#include <cstdint>
#include <vector>

namespace solo_rig {

enum class SampleRepresentation {
    Float32,
};

enum class AudioFormatValidationError {
    InvalidSampleRate,
    InvalidBlockSize,
    InvalidChannelCount,
};

struct AudioFormat {
    double sample_rate_hz{};
    std::uint32_t max_block_size{};
    std::uint32_t channel_count{};
    SampleRepresentation sample_representation{SampleRepresentation::Float32};

    [[nodiscard]] std::vector<AudioFormatValidationError> Validate() const;
};

} // namespace solo_rig
