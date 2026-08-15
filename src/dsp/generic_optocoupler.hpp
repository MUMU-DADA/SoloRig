#pragma once

#include <cstdint>

namespace solo_rig {

struct GenericOptocouplerModel {
    double dark_resistance_ohms{1.0e6};
    double lit_resistance_ohms{100.0};
    double emitter_threshold_volts{1.0};
    double emitter_scale_volts{4.0};
    double response_exponent{1.0};
    double attack_seconds{0.01};
    double release_seconds{0.1};
    double maximum_drive_volts{5.0};
    double initial_illumination{};
};

struct GenericOptocouplerEvaluation {
    double target_illumination{};
    double resistance_ohms{};
};

enum class GenericOptocouplerError {
    None,
    InvalidModel,
    InvalidSampleRate,
    InvalidOversampling,
    NonFinite,
    NotPrepared,
};

[[nodiscard]] bool ValidateGenericOptocouplerModel(
    const GenericOptocouplerModel& model) noexcept;
[[nodiscard]] GenericOptocouplerError EvaluateGenericOptocoupler(
    const GenericOptocouplerModel& model,
    double emitter_drive_volts,
    double illumination,
    GenericOptocouplerEvaluation& output) noexcept;

class GenericOptocouplerRuntime final {
public:
    [[nodiscard]] GenericOptocouplerError Prepare(const GenericOptocouplerModel& model,
                                                  double sample_rate_hz,
                                                  std::uint32_t oversampling) noexcept;
    [[nodiscard]] GenericOptocouplerError SetSampleRate(double sample_rate_hz) noexcept;
    [[nodiscard]] GenericOptocouplerError Process(double emitter_drive_volts,
                                                  double& resistance_ohms) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] double CurrentIllumination() const noexcept { return illumination_; }

private:
    [[nodiscard]] GenericOptocouplerError ConfigureRate(double sample_rate_hz) noexcept;

    GenericOptocouplerModel model_{};
    double attack_alpha_{1.0};
    double release_alpha_{1.0};
    double illumination_{};
    std::uint32_t oversampling_{1U};
    bool prepared_{};
};

} // namespace solo_rig
