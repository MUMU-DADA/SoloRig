#pragma once

#include <atomic>
#include <cstdint>

namespace solo_rig {

class RealtimeLoadMeter {
public:
    void Configure(double sample_rate_hz, std::uint32_t block_size) noexcept;
    void RecordProcessingNanoseconds(std::uint64_t elapsed_nanoseconds) noexcept;

    [[nodiscard]] double LastLoadFraction() const noexcept;
    [[nodiscard]] double PeakLoadFraction() const noexcept;

private:
    std::atomic<std::uint64_t> block_duration_nanoseconds_{1};
    std::atomic<std::uint64_t> last_elapsed_nanoseconds_{0};
    std::atomic<std::uint64_t> peak_elapsed_nanoseconds_{0};
};

} // namespace solo_rig
