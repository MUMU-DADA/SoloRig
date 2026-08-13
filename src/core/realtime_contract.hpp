#pragma once

#include <cstdint>

namespace solo_rig {

// This is an architectural contract, not a claim that every future module is
// already implemented.  The values describe the current shared C++ runtime
// boundary: compilation and storage ownership happen before Process, while
// Process is a noexcept, file-free, JSON-free and lock-free operation.
struct RealtimeContract {
    bool process_noexcept{true};
    bool process_allocates{false};
    bool process_reads_files{false};
    bool process_parses_json{false};
    bool process_locks{false};
    bool prepare_owns_compilation{true};
    std::uint32_t maximum_oversampling{16U};
    std::uint32_t sample_rate_hz{};
    std::uint32_t max_block_size{};
    std::uint32_t channel_count{};
    std::uint32_t declared_latency_samples{};
    double estimated_cpu_percent{};
};

[[nodiscard]] constexpr RealtimeContract DefaultRealtimeContract() noexcept {
    return {};
}

[[nodiscard]] constexpr bool IsRealtimeSafe(const RealtimeContract& contract) noexcept {
    return contract.process_noexcept && !contract.process_allocates && !contract.process_reads_files &&
           !contract.process_parses_json && !contract.process_locks && contract.prepare_owns_compilation &&
           contract.maximum_oversampling >= 1U && contract.maximum_oversampling <= 16U;
}

} // namespace solo_rig
