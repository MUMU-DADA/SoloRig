#include "standalone/audio_resampler.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace solo_rig {

std::uint32_t ResampledBlockSize(std::uint32_t source_sample_count,
                                 double source_rate_hz,
                                 double destination_rate_hz) noexcept {
    if (source_sample_count == 0U || !std::isfinite(source_rate_hz) || source_rate_hz <= 0.0 ||
        !std::isfinite(destination_rate_hz) || destination_rate_hz <= 0.0) {
        return 0U;
    }
    const auto scaled = static_cast<double>(source_sample_count) * destination_rate_hz / source_rate_hz;
    if (!std::isfinite(scaled) || scaled > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
        return 0U;
    }
    return std::max(1U, static_cast<std::uint32_t>(std::lround(scaled)));
}

bool ResampleLinear(std::span<const float> source,
                    std::span<float> destination,
                    double source_rate_hz,
                    double destination_rate_hz) noexcept {
    if (destination.empty() || source.empty() || !std::isfinite(source_rate_hz) || source_rate_hz <= 0.0 ||
        !std::isfinite(destination_rate_hz) || destination_rate_hz <= 0.0) {
        if (source.empty()) {
            std::fill(destination.begin(), destination.end(), 0.0F);
        }
        return false;
    }
    const auto source_per_destination = source_rate_hz / destination_rate_hz;
    for (std::size_t index = 0U; index < destination.size(); ++index) {
        const auto source_position = static_cast<double>(index) * source_per_destination;
        const auto source_floor = std::floor(source_position);
        const auto source_index = static_cast<std::size_t>(std::min(
            static_cast<double>(source.size() - 1U), source_floor));
        const auto next_index = std::min(source_index + 1U, source.size() - 1U);
        const auto fraction = static_cast<float>(source_position - static_cast<double>(source_index));
        destination[index] = source[source_index] * (1.0F - fraction) + source[next_index] * fraction;
    }
    return true;
}

} // namespace solo_rig
