#pragma once

#include <cstdint>

namespace solo_rig {

enum class GenericLfoWaveform {
    Sine,
    Triangle,
    Saw,
    Square,
};

struct GenericLfoModel {
    GenericLfoWaveform waveform{GenericLfoWaveform::Sine};
    double frequency_hz{1.0};
    double output_amplitude{1.0};
    double output_offset{};
    double maximum_output{1.0};
    double initial_phase_cycles{};
    double duty_cycle{0.5};
};

enum class GenericLfoError {
    None,
    InvalidModel,
    InvalidSampleRate,
    InvalidOversampling,
    NonFinite,
    NotPrepared,
};

[[nodiscard]] bool ValidateGenericLfoModel(const GenericLfoModel& model) noexcept;
[[nodiscard]] GenericLfoError EvaluateGenericLfoOutput(const GenericLfoModel& model,
                                                       double phase_cycles,
                                                       double& output) noexcept;

class GenericLfoRuntime final {
public:
    [[nodiscard]] GenericLfoError Prepare(const GenericLfoModel& model,
                                          double sample_rate_hz,
                                          std::uint32_t oversampling) noexcept;
    [[nodiscard]] GenericLfoError SetSampleRate(double sample_rate_hz) noexcept;
    [[nodiscard]] GenericLfoError Process(double& output) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] double CurrentPhaseCycles() const noexcept { return phase_cycles_; }

private:
    [[nodiscard]] GenericLfoError ConfigureRate(double sample_rate_hz) noexcept;

    GenericLfoModel model_{};
    double phase_increment_{};
    double phase_cycles_{};
    std::uint32_t oversampling_{1U};
    bool prepared_{};
};

} // namespace solo_rig
