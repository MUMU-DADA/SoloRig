#pragma once

#include <cstdint>
#include <vector>

namespace solo_rig {

struct GenericBbdModel {
    std::uint32_t bucket_count{512U};
    double clock_frequency_hz{100000.0};
    double transfer_efficiency{0.99};
    double feedback{};
    double wet{1.0};
    double dry{};
    double maximum_input{1.0};
};

enum class GenericBbdError {
    None,
    InvalidModel,
    InvalidSampleRate,
    InvalidOversampling,
    NonFinite,
    NotPrepared,
};

[[nodiscard]] bool ValidateGenericBbdModel(const GenericBbdModel& model) noexcept;

class GenericBbdRuntime final {
public:
    GenericBbdRuntime() = default;
    GenericBbdRuntime(GenericBbdRuntime&&) noexcept = default;
    GenericBbdRuntime& operator=(GenericBbdRuntime&&) noexcept = default;
    GenericBbdRuntime(const GenericBbdRuntime&) = delete;
    GenericBbdRuntime& operator=(const GenericBbdRuntime&) = delete;

    [[nodiscard]] GenericBbdError Prepare(const GenericBbdModel& model,
                                           double sample_rate_hz,
                                           std::uint32_t oversampling) noexcept;
    [[nodiscard]] GenericBbdError SetSampleRate(double sample_rate_hz) noexcept;
    [[nodiscard]] GenericBbdError Process(double input, double& output) noexcept;
    void Reset() noexcept;
    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] double DelaySeconds() const noexcept { return delay_seconds_; }
    [[nodiscard]] std::uint32_t RingSize() const noexcept {
        return static_cast<std::uint32_t>(delay_line_.size());
    }

private:
    [[nodiscard]] GenericBbdError ConfigureStorage(double sample_rate_hz) noexcept;

    GenericBbdModel model_{};
    std::vector<double> delay_line_;
    double sample_rate_hz_{};
    double delay_seconds_{};
    double delay_samples_{};
    double efficiency_power_{};
    std::uint32_t oversampling_{1U};
    std::uint32_t write_position_{};
    bool prepared_{};
};

} // namespace solo_rig
