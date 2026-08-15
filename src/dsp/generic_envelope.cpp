#include "dsp/generic_envelope.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

namespace {

constexpr std::uint32_t kMaximumOversampling = 16U;
constexpr double kMaximumMagnitude = 1.0e6;
constexpr double kMaximumTimeSeconds = 60.0;

double DetectorTarget(const GenericEnvelopeModel& model, double signal_voltage) noexcept {
    const double bounded_input = std::clamp(signal_voltage,
        -model.maximum_input, model.maximum_input);
    return std::min(std::fabs(bounded_input) * model.input_gain, model.detector_ceiling);
}

double SmoothingAlpha(double substep_seconds, double time_seconds) noexcept {
    return time_seconds == 0.0 ? 1.0 : substep_seconds / (time_seconds + substep_seconds);
}

} // namespace

bool ValidateGenericEnvelopeModel(const GenericEnvelopeModel& model) noexcept {
    const double values[] = {model.input_gain, model.attack_seconds, model.release_seconds,
        model.detector_ceiling, model.output_scale, model.maximum_input,
        model.maximum_output, model.initial_envelope};
    for (const double value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return model.input_gain >= 0.0 && model.input_gain <= kMaximumMagnitude &&
           model.attack_seconds >= 0.0 && model.attack_seconds <= kMaximumTimeSeconds &&
           model.release_seconds >= 0.0 && model.release_seconds <= kMaximumTimeSeconds &&
           model.detector_ceiling >= 1.0e-6 &&
           model.detector_ceiling <= kMaximumMagnitude && model.output_scale >= 0.0 &&
           model.output_scale <= kMaximumMagnitude && model.maximum_input >= 1.0e-6 &&
           model.maximum_input <= kMaximumMagnitude && model.maximum_output >= 1.0e-6 &&
           model.maximum_output <= kMaximumMagnitude && model.initial_envelope >= 0.0 &&
           model.initial_envelope <= model.detector_ceiling;
}

GenericEnvelopeError EvaluateGenericEnvelopeTarget(const GenericEnvelopeModel& model,
                                                   double signal_voltage,
                                                   double& target) noexcept {
    target = 0.0;
    if (!ValidateGenericEnvelopeModel(model)) {
        return GenericEnvelopeError::InvalidModel;
    }
    if (!std::isfinite(signal_voltage)) {
        return GenericEnvelopeError::NonFinite;
    }
    target = DetectorTarget(model, signal_voltage);
    if (!std::isfinite(target)) {
        target = 0.0;
        return GenericEnvelopeError::NonFinite;
    }
    return GenericEnvelopeError::None;
}

GenericEnvelopeError GenericEnvelopeRuntime::ConfigureRate(double sample_rate_hz) noexcept {
    const double processing_rate_hz = sample_rate_hz * static_cast<double>(oversampling_);
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0 ||
        !std::isfinite(processing_rate_hz) || processing_rate_hz <= 0.0) {
        return GenericEnvelopeError::InvalidSampleRate;
    }
    const double substep_seconds = 1.0 / processing_rate_hz;
    const double attack_alpha = SmoothingAlpha(substep_seconds, model_.attack_seconds);
    const double release_alpha = SmoothingAlpha(substep_seconds, model_.release_seconds);
    if (!std::isfinite(attack_alpha) || attack_alpha <= 0.0 || attack_alpha > 1.0 ||
        !std::isfinite(release_alpha) || release_alpha <= 0.0 || release_alpha > 1.0) {
        return GenericEnvelopeError::InvalidSampleRate;
    }
    attack_alpha_ = attack_alpha;
    release_alpha_ = release_alpha;
    return GenericEnvelopeError::None;
}

GenericEnvelopeError GenericEnvelopeRuntime::Prepare(const GenericEnvelopeModel& model,
                                                     double sample_rate_hz,
                                                     std::uint32_t oversampling) noexcept {
    prepared_ = false;
    if (!ValidateGenericEnvelopeModel(model)) {
        return GenericEnvelopeError::InvalidModel;
    }
    if (oversampling == 0U || oversampling > kMaximumOversampling) {
        return GenericEnvelopeError::InvalidOversampling;
    }
    model_ = model;
    oversampling_ = oversampling;
    const auto rate_result = ConfigureRate(sample_rate_hz);
    if (rate_result != GenericEnvelopeError::None) {
        return rate_result;
    }
    prepared_ = true;
    Reset();
    return GenericEnvelopeError::None;
}

GenericEnvelopeError GenericEnvelopeRuntime::SetSampleRate(double sample_rate_hz) noexcept {
    if (!prepared_) {
        return GenericEnvelopeError::NotPrepared;
    }
    const auto rate_result = ConfigureRate(sample_rate_hz);
    if (rate_result != GenericEnvelopeError::None) {
        return rate_result;
    }
    Reset();
    return GenericEnvelopeError::None;
}

GenericEnvelopeError GenericEnvelopeRuntime::Process(double signal_voltage,
                                                     double& output) noexcept {
    output = 0.0;
    if (!prepared_) {
        return GenericEnvelopeError::NotPrepared;
    }
    if (!std::isfinite(signal_voltage)) {
        return GenericEnvelopeError::NonFinite;
    }
    const double target = DetectorTarget(model_, signal_voltage);
    for (std::uint32_t substep = 0U; substep < oversampling_; ++substep) {
        const double alpha = target > envelope_ ? attack_alpha_ : release_alpha_;
        envelope_ += alpha * (target - envelope_);
        envelope_ = std::clamp(envelope_, 0.0, model_.detector_ceiling);
    }
    output = std::clamp(envelope_ * model_.output_scale, 0.0, model_.maximum_output);
    if (!std::isfinite(envelope_) || !std::isfinite(output)) {
        output = 0.0;
        return GenericEnvelopeError::NonFinite;
    }
    return GenericEnvelopeError::None;
}

void GenericEnvelopeRuntime::Reset() noexcept {
    envelope_ = model_.initial_envelope;
}

} // namespace solo_rig
