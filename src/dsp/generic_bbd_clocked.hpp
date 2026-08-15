#pragma once

#include <cstdint>
#include <vector>

namespace solo_rig {

// Self-authored generic BBD model with an explicit per-sample clock control.
// The clock control is interpreted as octave offset from clock_frequency_hz.
struct GenericBbdClockedModel {
    std::uint32_t bucket_count{512U};
    double clock_frequency_hz{100000.0};
    double minimum_clock_frequency_hz{1000.0};
    double maximum_clock_frequency_hz{1.0e7};
    double clock_control_octaves{1.0};
    double transfer_efficiency{0.99};
    double feedback{};
    double wet{1.0};
    double dry{};
    double maximum_input{1.0};
};

enum class GenericBbdClockedError {
    None,
    InvalidModel,
    InvalidSampleRate,
    InvalidOversampling,
    NonFinite,
    NotPrepared,
};

[[nodiscard]] bool ValidateGenericBbdClockedModel(
    const GenericBbdClockedModel& model) noexcept;

class GenericBbdClockedRuntime final {
public:
    GenericBbdClockedRuntime() = default;
    GenericBbdClockedRuntime(GenericBbdClockedRuntime&&) noexcept = default;
    GenericBbdClockedRuntime& operator=(GenericBbdClockedRuntime&&) noexcept = default;
    GenericBbdClockedRuntime(const GenericBbdClockedRuntime&) = delete;
    GenericBbdClockedRuntime& operator=(const GenericBbdClockedRuntime&) = delete;

    [[nodiscard]] GenericBbdClockedError Prepare(const GenericBbdClockedModel& model,
                                                  double sample_rate_hz,
                                                  std::uint32_t oversampling) noexcept;
    [[nodiscard]] GenericBbdClockedError SetSampleRate(double sample_rate_hz) noexcept;
    [[nodiscard]] GenericBbdClockedError Process(double input, double clock_control,
                                                 double& output) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] std::uint32_t RingSize() const noexcept {
        return static_cast<std::uint32_t>(delay_line_.size());
    }
    [[nodiscard]] double CurrentClockFrequencyHz() const noexcept {
        return current_clock_frequency_hz_;
    }

private:
    [[nodiscard]] GenericBbdClockedError ConfigureStorage(double sample_rate_hz) noexcept;

    GenericBbdClockedModel model_{};
    std::vector<double> delay_line_;
    double sample_rate_hz_{};
    double processing_rate_hz_{};
    double delay_samples_{};
    double efficiency_power_{};
    double current_clock_frequency_hz_{};
    std::uint32_t oversampling_{1U};
    std::uint32_t write_position_{};
    bool prepared_{};
};

} // namespace solo_rig
