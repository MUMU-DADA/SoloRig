#pragma once

#include <cstdint>

namespace solo_rig {

enum class GenericVcoWaveform {
    Sine,
    Triangle,
    Saw,
    Square,
};

struct GenericVcoModel {
    GenericVcoWaveform waveform{GenericVcoWaveform::Sine};
    double base_frequency_hz{440.0};
    double minimum_frequency_hz{20.0};
    double maximum_frequency_hz{20000.0};
    double volts_per_octave{1.0};
    double reference_voltage{};
    double output_amplitude{1.0};
    double output_offset{};
    double initial_phase_cycles{};
    double maximum_control_voltage{10.0};
};

enum class GenericVcoError {
    None,
    InvalidModel,
    InvalidSampleRate,
    InvalidOversampling,
    NonFinite,
    NotPrepared,
};

[[nodiscard]] bool ValidateGenericVcoModel(const GenericVcoModel& model) noexcept;
[[nodiscard]] double EvaluateGenericVcoWaveform(GenericVcoWaveform waveform,
                                                double phase_cycles) noexcept;

class GenericVcoRuntime final {
public:
    [[nodiscard]] GenericVcoError Prepare(const GenericVcoModel& model,
                                          double sample_rate_hz,
                                          std::uint32_t oversampling) noexcept;
    [[nodiscard]] GenericVcoError SetSampleRate(double sample_rate_hz) noexcept;
    [[nodiscard]] GenericVcoError Process(double control_voltage, double& output) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] double CurrentPhaseCycles() const noexcept { return phase_cycles_; }
    [[nodiscard]] double CurrentFrequencyHz() const noexcept { return current_frequency_hz_; }

private:
    [[nodiscard]] GenericVcoError ValidateRate(double sample_rate_hz,
                                              double& processing_rate_hz) const noexcept;

    GenericVcoModel model_{};
    double sample_rate_hz_{};
    double processing_rate_hz_{};
    double phase_cycles_{};
    double current_frequency_hz_{};
    std::uint32_t oversampling_{1U};
    bool prepared_{};
};

} // namespace solo_rig
