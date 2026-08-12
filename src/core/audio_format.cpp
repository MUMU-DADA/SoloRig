#include "core/audio_format.hpp"

#include <cmath>

namespace solo_rig {

std::vector<AudioFormatValidationError> AudioFormat::Validate() const {
    constexpr double minimum_sample_rate_hz = 8000.0;
    constexpr double maximum_sample_rate_hz = 384000.0;

    std::vector<AudioFormatValidationError> errors;

    if (!std::isfinite(sample_rate_hz) || sample_rate_hz < minimum_sample_rate_hz ||
        sample_rate_hz > maximum_sample_rate_hz) {
        errors.push_back(AudioFormatValidationError::InvalidSampleRate);
    }

    if (max_block_size == 0) {
        errors.push_back(AudioFormatValidationError::InvalidBlockSize);
    }

    if (channel_count == 0) {
        errors.push_back(AudioFormatValidationError::InvalidChannelCount);
    }

    return errors;
}

} // namespace solo_rig
