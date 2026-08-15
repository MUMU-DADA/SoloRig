#pragma once

#include <cstdint>
#include <span>

namespace solo_rig {

[[nodiscard]] std::uint32_t ResampledBlockSize(std::uint32_t source_sample_count,
                                                double source_rate_hz,
                                                double destination_rate_hz) noexcept;

// Stateless linear interpolation used only at the device/block boundary. The
// host keeps the internal graph at 192 kHz; this helper never allocates and
// leaves clock-domain synchronization to the higher-level host policy.
[[nodiscard]] bool ResampleLinear(std::span<const float> source,
                                   std::span<float> destination,
                                   double source_rate_hz,
                                   double destination_rate_hz) noexcept;

} // namespace solo_rig
