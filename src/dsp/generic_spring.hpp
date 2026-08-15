#pragma once

#include <array>
#include <cstdint>

namespace solo_rig {

inline constexpr std::uint32_t kMaximumGenericSpringModes = 16U;

struct GenericSpringModel {
    std::uint32_t mode_count{8U};
    double base_frequency_hz{90.0};
    double mode_spacing_hz{73.0};
    double dispersion{0.35};
    double decay_seconds{1.8};
    double high_frequency_damping{4.0};
    double drive_gain{1.0};
    double pickup_gain{1.0};
    double wet{1.0};
    double dry{};
    double maximum_input{1.0};
    double maximum_output{1.0};
};

struct GenericSpringMode {
    double frequency_hz{};
    double decay_seconds{};
    double pole_radius{};
    double cosine{};
    double sine{};
    double input_coefficient{};
};

enum class GenericSpringError {
    None,
    InvalidModel,
    InvalidMode,
    InvalidSampleRate,
    InvalidOversampling,
    NonFinite,
    NotPrepared,
};

[[nodiscard]] bool ValidateGenericSpringModel(const GenericSpringModel& model) noexcept;
[[nodiscard]] GenericSpringError EvaluateGenericSpringMode(
    const GenericSpringModel& model,
    std::uint32_t mode_index,
    double processing_rate_hz,
    GenericSpringMode& output) noexcept;

class GenericSpringRuntime final {
public:
    [[nodiscard]] GenericSpringError Prepare(const GenericSpringModel& model,
                                             double sample_rate_hz,
                                             std::uint32_t oversampling) noexcept;
    [[nodiscard]] GenericSpringError SetSampleRate(double sample_rate_hz) noexcept;
    [[nodiscard]] GenericSpringError Process(double input, double& output) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] double CurrentStateEnergy() const noexcept;

private:
    [[nodiscard]] GenericSpringError ConfigureRate(double sample_rate_hz) noexcept;

    GenericSpringModel model_{};
    std::array<GenericSpringMode, kMaximumGenericSpringModes> modes_{};
    std::array<double, kMaximumGenericSpringModes> real_state_{};
    std::array<double, kMaximumGenericSpringModes> imaginary_state_{};
    std::uint32_t oversampling_{1U};
    bool prepared_{};
};

} // namespace solo_rig
