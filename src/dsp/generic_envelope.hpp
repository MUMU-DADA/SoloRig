#pragma once

#include <cstdint>

namespace solo_rig {

struct GenericEnvelopeModel {
    double input_gain{1.0};
    double attack_seconds{0.01};
    double release_seconds{0.1};
    double detector_ceiling{1.0};
    double output_scale{1.0};
    double maximum_input{1.0};
    double maximum_output{1.0};
    double initial_envelope{};
};

enum class GenericEnvelopeError {
    None,
    InvalidModel,
    InvalidSampleRate,
    InvalidOversampling,
    NonFinite,
    NotPrepared,
};

[[nodiscard]] bool ValidateGenericEnvelopeModel(const GenericEnvelopeModel& model) noexcept;
[[nodiscard]] GenericEnvelopeError EvaluateGenericEnvelopeTarget(
    const GenericEnvelopeModel& model, double signal_voltage, double& target) noexcept;

class GenericEnvelopeRuntime final {
public:
    [[nodiscard]] GenericEnvelopeError Prepare(const GenericEnvelopeModel& model,
                                               double sample_rate_hz,
                                               std::uint32_t oversampling) noexcept;
    [[nodiscard]] GenericEnvelopeError SetSampleRate(double sample_rate_hz) noexcept;
    [[nodiscard]] GenericEnvelopeError Process(double signal_voltage, double& output) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] double CurrentEnvelope() const noexcept { return envelope_; }

private:
    [[nodiscard]] GenericEnvelopeError ConfigureRate(double sample_rate_hz) noexcept;

    GenericEnvelopeModel model_{};
    double attack_alpha_{1.0};
    double release_alpha_{1.0};
    double envelope_{};
    std::uint32_t oversampling_{1U};
    bool prepared_{};
};

} // namespace solo_rig
