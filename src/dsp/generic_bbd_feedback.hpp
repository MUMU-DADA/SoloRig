#pragma once

#include <cstdint>
#include <vector>

namespace solo_rig {

// Self-authored generic BBD feedback model. Feedback is explicitly delayed:
// the previous substep's output is the only feedback state used by Process.
struct GenericBbdFeedbackModel {
    std::uint32_t bucket_count{512U};
    double clock_frequency_hz{100000.0};
    double minimum_clock_frequency_hz{1000.0};
    double maximum_clock_frequency_hz{1.0e7};
    double clock_control_octaves{1.0};
    double transfer_efficiency{0.99};
    double feedback_gain{};
    double wet{1.0};
    double dry{};
    double maximum_input{1.0};
};

enum class GenericBbdFeedbackError {
    None,
    InvalidModel,
    InvalidSampleRate,
    InvalidOversampling,
    NonFinite,
    NotPrepared,
};

[[nodiscard]] bool ValidateGenericBbdFeedbackModel(
    const GenericBbdFeedbackModel& model) noexcept;

class GenericBbdFeedbackRuntime final {
public:
    GenericBbdFeedbackRuntime() = default;
    GenericBbdFeedbackRuntime(GenericBbdFeedbackRuntime&&) noexcept = default;
    GenericBbdFeedbackRuntime& operator=(GenericBbdFeedbackRuntime&&) noexcept = default;
    GenericBbdFeedbackRuntime(const GenericBbdFeedbackRuntime&) = delete;
    GenericBbdFeedbackRuntime& operator=(const GenericBbdFeedbackRuntime&) = delete;

    [[nodiscard]] GenericBbdFeedbackError Prepare(const GenericBbdFeedbackModel& model,
                                                  double sample_rate_hz,
                                                  std::uint32_t oversampling) noexcept;
    [[nodiscard]] GenericBbdFeedbackError SetSampleRate(double sample_rate_hz) noexcept;
    [[nodiscard]] GenericBbdFeedbackError Process(double input, double clock_control,
                                                  double& output) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] std::uint32_t RingSize() const noexcept {
        return static_cast<std::uint32_t>(delay_line_.size());
    }
    [[nodiscard]] double PreviousOutput() const noexcept { return previous_output_; }
    [[nodiscard]] double CurrentClockFrequencyHz() const noexcept {
        return current_clock_frequency_hz_;
    }

private:
    [[nodiscard]] GenericBbdFeedbackError ConfigureStorage(double sample_rate_hz) noexcept;

    GenericBbdFeedbackModel model_{};
    std::vector<double> delay_line_;
    double sample_rate_hz_{};
    double processing_rate_hz_{};
    double delay_samples_{};
    double efficiency_power_{};
    double previous_output_{};
    double current_clock_frequency_hz_{};
    std::uint32_t oversampling_{1U};
    std::uint32_t write_position_{};
    bool prepared_{};
};

} // namespace solo_rig
