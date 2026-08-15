#pragma once

#include <cstdint>

namespace solo_rig {

struct GenericVcaModel {
    double reference_control_voltage{};
    double reference_gain{1.0};
    double gain_decibels_per_volt{6.020599913279624};
    double minimum_gain{};
    double maximum_gain{1.0};
    double control_smoothing_seconds{};
    double initial_control_voltage{};
    double maximum_control_voltage{10.0};
    double maximum_input{1.0};
    double maximum_output{1.0};
};

enum class GenericVcaError {
    None,
    InvalidModel,
    InvalidSampleRate,
    InvalidOversampling,
    NonFinite,
    NotPrepared,
};

[[nodiscard]] bool ValidateGenericVcaModel(const GenericVcaModel& model) noexcept;
[[nodiscard]] GenericVcaError EvaluateGenericVcaTargetGain(const GenericVcaModel& model,
                                                           double control_voltage,
                                                           double& target_gain) noexcept;

class GenericVcaRuntime final {
public:
    [[nodiscard]] GenericVcaError Prepare(const GenericVcaModel& model,
                                          double sample_rate_hz,
                                          std::uint32_t oversampling) noexcept;
    [[nodiscard]] GenericVcaError SetSampleRate(double sample_rate_hz) noexcept;
    [[nodiscard]] GenericVcaError Process(double signal_voltage,
                                          double control_voltage,
                                          double& output) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] double CurrentGain() const noexcept { return current_gain_; }

private:
    [[nodiscard]] GenericVcaError ConfigureRate(double sample_rate_hz) noexcept;

    GenericVcaModel model_{};
    double smoothing_alpha_{1.0};
    double current_gain_{1.0};
    std::uint32_t oversampling_{1U};
    bool prepared_{};
};

} // namespace solo_rig
