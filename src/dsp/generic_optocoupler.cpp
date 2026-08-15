#include "dsp/generic_optocoupler.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

namespace {

constexpr std::uint32_t kMaximumOversampling = 16U;
constexpr double kMinimumResistanceOhms = 1.0e-3;
constexpr double kMaximumResistanceOhms = 1.0e12;
constexpr double kMaximumMagnitude = 1.0e6;
constexpr double kMaximumTimeSeconds = 60.0;

double TargetIllumination(const GenericOptocouplerModel& model,
                          double emitter_drive_volts) noexcept {
    const double bounded_drive = std::clamp(emitter_drive_volts,
        0.0, model.maximum_drive_volts);
    const double normalized = std::clamp(
        (bounded_drive - model.emitter_threshold_volts) / model.emitter_scale_volts,
        0.0, 1.0);
    return std::pow(normalized, model.response_exponent);
}

double ResistanceOhms(const GenericOptocouplerModel& model, double illumination) noexcept {
    if (illumination <= 0.0) {
        return model.dark_resistance_ohms;
    }
    if (illumination >= 1.0) {
        return model.lit_resistance_ohms;
    }
    const double log_resistance = std::log(model.dark_resistance_ohms) +
        illumination * (std::log(model.lit_resistance_ohms) -
                        std::log(model.dark_resistance_ohms));
    return std::clamp(std::exp(log_resistance),
        model.lit_resistance_ohms, model.dark_resistance_ohms);
}

double SmoothingAlpha(double substep_seconds, double time_seconds) noexcept {
    return time_seconds == 0.0 ? 1.0 : substep_seconds / (time_seconds + substep_seconds);
}

} // namespace

bool ValidateGenericOptocouplerModel(const GenericOptocouplerModel& model) noexcept {
    const double values[] = {model.dark_resistance_ohms, model.lit_resistance_ohms,
        model.emitter_threshold_volts, model.emitter_scale_volts,
        model.response_exponent, model.attack_seconds, model.release_seconds,
        model.maximum_drive_volts, model.initial_illumination};
    for (const double value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return model.dark_resistance_ohms >= kMinimumResistanceOhms &&
           model.dark_resistance_ohms <= kMaximumResistanceOhms &&
           model.lit_resistance_ohms >= kMinimumResistanceOhms &&
           model.lit_resistance_ohms <= model.dark_resistance_ohms &&
           model.emitter_threshold_volts >= 0.0 &&
           model.emitter_threshold_volts <= model.maximum_drive_volts &&
           model.emitter_scale_volts >= 1.0e-6 &&
           model.emitter_scale_volts <= kMaximumMagnitude &&
           model.response_exponent >= 0.1 && model.response_exponent <= 10.0 &&
           model.attack_seconds >= 0.0 && model.attack_seconds <= kMaximumTimeSeconds &&
           model.release_seconds >= 0.0 && model.release_seconds <= kMaximumTimeSeconds &&
           model.maximum_drive_volts >= 1.0e-6 &&
           model.maximum_drive_volts <= kMaximumMagnitude &&
           model.initial_illumination >= 0.0 && model.initial_illumination <= 1.0;
}

GenericOptocouplerError EvaluateGenericOptocoupler(
    const GenericOptocouplerModel& model,
    double emitter_drive_volts,
    double illumination,
    GenericOptocouplerEvaluation& output) noexcept {
    output = {};
    if (!ValidateGenericOptocouplerModel(model)) {
        return GenericOptocouplerError::InvalidModel;
    }
    if (!std::isfinite(emitter_drive_volts) || !std::isfinite(illumination)) {
        return GenericOptocouplerError::NonFinite;
    }
    if (illumination < 0.0 || illumination > 1.0) {
        return GenericOptocouplerError::NonFinite;
    }
    output.target_illumination = TargetIllumination(model, emitter_drive_volts);
    output.resistance_ohms = ResistanceOhms(model, illumination);
    if (!std::isfinite(output.target_illumination) ||
        !std::isfinite(output.resistance_ohms)) {
        output = {};
        return GenericOptocouplerError::NonFinite;
    }
    return GenericOptocouplerError::None;
}

GenericOptocouplerError GenericOptocouplerRuntime::ConfigureRate(
    double sample_rate_hz) noexcept {
    const double processing_rate_hz = sample_rate_hz * static_cast<double>(oversampling_);
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0 ||
        !std::isfinite(processing_rate_hz) || processing_rate_hz <= 0.0) {
        return GenericOptocouplerError::InvalidSampleRate;
    }
    const double substep_seconds = 1.0 / processing_rate_hz;
    const double attack_alpha = SmoothingAlpha(substep_seconds, model_.attack_seconds);
    const double release_alpha = SmoothingAlpha(substep_seconds, model_.release_seconds);
    if (!std::isfinite(attack_alpha) || attack_alpha <= 0.0 || attack_alpha > 1.0 ||
        !std::isfinite(release_alpha) || release_alpha <= 0.0 || release_alpha > 1.0) {
        return GenericOptocouplerError::InvalidSampleRate;
    }
    attack_alpha_ = attack_alpha;
    release_alpha_ = release_alpha;
    return GenericOptocouplerError::None;
}

GenericOptocouplerError GenericOptocouplerRuntime::Prepare(
    const GenericOptocouplerModel& model,
    double sample_rate_hz,
    std::uint32_t oversampling) noexcept {
    prepared_ = false;
    if (!ValidateGenericOptocouplerModel(model)) {
        return GenericOptocouplerError::InvalidModel;
    }
    if (oversampling == 0U || oversampling > kMaximumOversampling) {
        return GenericOptocouplerError::InvalidOversampling;
    }
    model_ = model;
    oversampling_ = oversampling;
    const auto rate_result = ConfigureRate(sample_rate_hz);
    if (rate_result != GenericOptocouplerError::None) {
        return rate_result;
    }
    prepared_ = true;
    Reset();
    return GenericOptocouplerError::None;
}

GenericOptocouplerError GenericOptocouplerRuntime::SetSampleRate(
    double sample_rate_hz) noexcept {
    if (!prepared_) {
        return GenericOptocouplerError::NotPrepared;
    }
    const auto rate_result = ConfigureRate(sample_rate_hz);
    if (rate_result != GenericOptocouplerError::None) {
        return rate_result;
    }
    Reset();
    return GenericOptocouplerError::None;
}

GenericOptocouplerError GenericOptocouplerRuntime::Process(
    double emitter_drive_volts,
    double& resistance_ohms) noexcept {
    resistance_ohms = 0.0;
    if (!prepared_) {
        return GenericOptocouplerError::NotPrepared;
    }
    if (!std::isfinite(emitter_drive_volts)) {
        return GenericOptocouplerError::NonFinite;
    }
    const double target = TargetIllumination(model_, emitter_drive_volts);
    for (std::uint32_t substep = 0U; substep < oversampling_; ++substep) {
        const double alpha = target > illumination_ ? attack_alpha_ : release_alpha_;
        illumination_ += alpha * (target - illumination_);
        illumination_ = std::clamp(illumination_, 0.0, 1.0);
    }
    resistance_ohms = ResistanceOhms(model_, illumination_);
    if (!std::isfinite(target) || !std::isfinite(illumination_) ||
        !std::isfinite(resistance_ohms)) {
        resistance_ohms = 0.0;
        return GenericOptocouplerError::NonFinite;
    }
    return GenericOptocouplerError::None;
}

void GenericOptocouplerRuntime::Reset() noexcept {
    illumination_ = model_.initial_illumination;
}

} // namespace solo_rig
