#include "core/realtime_load_meter.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

RealtimeLoadMeter::RealtimeLoadMeter(RealtimeLoadMeter&& other) noexcept {
    block_duration_nanoseconds_.store(
        other.block_duration_nanoseconds_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    last_elapsed_nanoseconds_.store(
        other.last_elapsed_nanoseconds_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    peak_elapsed_nanoseconds_.store(
        other.peak_elapsed_nanoseconds_.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

RealtimeLoadMeter& RealtimeLoadMeter::operator=(RealtimeLoadMeter&& other) noexcept {
    if (this != &other) {
        block_duration_nanoseconds_.store(
            other.block_duration_nanoseconds_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        last_elapsed_nanoseconds_.store(
            other.last_elapsed_nanoseconds_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        peak_elapsed_nanoseconds_.store(
            other.peak_elapsed_nanoseconds_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return *this;
}

void RealtimeLoadMeter::Configure(double sample_rate_hz, std::uint32_t block_size) noexcept {
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0 || block_size == 0) {
        block_duration_nanoseconds_.store(1, std::memory_order_relaxed);
        return;
    }

    const auto duration = static_cast<std::uint64_t>(
        std::llround((static_cast<double>(block_size) / sample_rate_hz) * 1'000'000'000.0));
    block_duration_nanoseconds_.store(std::max<std::uint64_t>(duration, 1), std::memory_order_relaxed);
    last_elapsed_nanoseconds_.store(0, std::memory_order_relaxed);
    peak_elapsed_nanoseconds_.store(0, std::memory_order_relaxed);
}

void RealtimeLoadMeter::RecordProcessingNanoseconds(std::uint64_t elapsed_nanoseconds) noexcept {
    last_elapsed_nanoseconds_.store(elapsed_nanoseconds, std::memory_order_relaxed);

    auto current_peak = peak_elapsed_nanoseconds_.load(std::memory_order_relaxed);
    while (current_peak < elapsed_nanoseconds &&
           !peak_elapsed_nanoseconds_.compare_exchange_weak(
               current_peak, elapsed_nanoseconds, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

double RealtimeLoadMeter::LastLoadFraction() const noexcept {
    return static_cast<double>(last_elapsed_nanoseconds_.load(std::memory_order_relaxed)) /
           static_cast<double>(block_duration_nanoseconds_.load(std::memory_order_relaxed));
}

double RealtimeLoadMeter::PeakLoadFraction() const noexcept {
    return static_cast<double>(peak_elapsed_nanoseconds_.load(std::memory_order_relaxed)) /
           static_cast<double>(block_duration_nanoseconds_.load(std::memory_order_relaxed));
}

} // namespace solo_rig
