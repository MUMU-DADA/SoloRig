#include "dsp/generic_vca.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

namespace {

constexpr std::uint32_t kMaximumOversampling = 16U;
constexpr double kMaximumMagnitude = 1.0e6;
constexpr double kMaximumDecibels = 240.0;
constexpr double kMaximumSmoothingSeconds = 60.0;

double TargetGain(const GenericVcaModel& model, double control_voltage) noexcept {
    const double bounded_control = std::clamp(control_voltage,
        -model.maximum_control_voltage, model.maximum_control_voltage);
    const double gain_decibels = std::clamp(
        (bounded_control - model.reference_control_voltage) * model.gain_decibels_per_volt,
        -kMaximumDecibels, kMaximumDecibels);
    return std::clamp(model.reference_gain * std::pow(10.0, gain_decibels / 20.0),
        model.minimum_gain, model.maximum_gain);
}

} // namespace

bool ValidateGenericVcaModel(const GenericVcaModel& model) noexcept {
    const double values[] = {model.reference_control_voltage, model.reference_gain,
        model.gain_decibels_per_volt, model.minimum_gain, model.maximum_gain,
        model.control_smoothing_seconds, model.initial_control_voltage,
        model.maximum_control_voltage, model.maximum_input, model.maximum_output};
    for (const double value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return std::fabs(model.reference_control_voltage) <= kMaximumMagnitude &&
           model.reference_gain >= 0.0 && model.reference_gain <= kMaximumMagnitude &&
           std::fabs(model.gain_decibels_per_volt) <= kMaximumDecibels &&
           model.minimum_gain >= 0.0 && model.minimum_gain <= model.reference_gain &&
           model.reference_gain <= model.maximum_gain &&
           model.maximum_gain <= kMaximumMagnitude &&
           model.control_smoothing_seconds >= 0.0 &&
           model.control_smoothing_seconds <= kMaximumSmoothingSeconds &&
           model.maximum_control_voltage >= 1.0e-6 &&
           model.maximum_control_voltage <= kMaximumMagnitude &&
           std::fabs(model.initial_control_voltage) <= model.maximum_control_voltage &&
           model.maximum_input >= 1.0e-6 && model.maximum_input <= kMaximumMagnitude &&
           model.maximum_output >= 1.0e-6 && model.maximum_output <= kMaximumMagnitude;
}

GenericVcaError EvaluateGenericVcaTargetGain(const GenericVcaModel& model,
                                             double control_voltage,
                                             double& target_gain) noexcept {
    target_gain = 0.0;
    if (!ValidateGenericVcaModel(model)) {
        return GenericVcaError::InvalidModel;
    }
    if (!std::isfinite(control_voltage)) {
        return GenericVcaError::NonFinite;
    }
    target_gain = TargetGain(model, control_voltage);
    if (!std::isfinite(target_gain)) {
        target_gain = 0.0;
        return GenericVcaError::NonFinite;
    }
    return GenericVcaError::None;
}

GenericVcaError GenericVcaRuntime::ConfigureRate(double sample_rate_hz) noexcept {
    const double processing_rate_hz = sample_rate_hz * static_cast<double>(oversampling_);
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0 ||
        !std::isfinite(processing_rate_hz) || processing_rate_hz <= 0.0) {
        return GenericVcaError::InvalidSampleRate;
    }
    const double substep_seconds = 1.0 / processing_rate_hz;
    smoothing_alpha_ = model_.control_smoothing_seconds == 0.0 ? 1.0 :
        substep_seconds / (model_.control_smoothing_seconds + substep_seconds);
    if (!std::isfinite(smoothing_alpha_) || smoothing_alpha_ <= 0.0 ||
        smoothing_alpha_ > 1.0) {
        return GenericVcaError::InvalidSampleRate;
    }
    return GenericVcaError::None;
}

GenericVcaError GenericVcaRuntime::Prepare(const GenericVcaModel& model,
                                           double sample_rate_hz,
                                           std::uint32_t oversampling) noexcept {
    prepared_ = false;
    if (!ValidateGenericVcaModel(model)) {
        return GenericVcaError::InvalidModel;
    }
    if (oversampling == 0U || oversampling > kMaximumOversampling) {
        return GenericVcaError::InvalidOversampling;
    }
    model_ = model;
    oversampling_ = oversampling;
    const auto rate_result = ConfigureRate(sample_rate_hz);
    if (rate_result != GenericVcaError::None) {
        return rate_result;
    }
    prepared_ = true;
    Reset();
    return GenericVcaError::None;
}

GenericVcaError GenericVcaRuntime::SetSampleRate(double sample_rate_hz) noexcept {
    if (!prepared_) {
        return GenericVcaError::NotPrepared;
    }
    const auto rate_result = ConfigureRate(sample_rate_hz);
    if (rate_result != GenericVcaError::None) {
        return rate_result;
    }
    Reset();
    return GenericVcaError::None;
}

GenericVcaError GenericVcaRuntime::Process(double signal_voltage,
                                           double control_voltage,
                                           double& output) noexcept {
    output = 0.0;
    if (!prepared_) {
        return GenericVcaError::NotPrepared;
    }
    if (!std::isfinite(signal_voltage) || !std::isfinite(control_voltage)) {
        return GenericVcaError::NonFinite;
    }
    const double bounded_input = std::clamp(signal_voltage,
        -model_.maximum_input, model_.maximum_input);
    const double target_gain = TargetGain(model_, control_voltage);
    for (std::uint32_t substep = 0U; substep < oversampling_; ++substep) {
        current_gain_ += smoothing_alpha_ * (target_gain - current_gain_);
        current_gain_ = std::clamp(current_gain_, model_.minimum_gain, model_.maximum_gain);
    }
    output = std::clamp(bounded_input * current_gain_,
        -model_.maximum_output, model_.maximum_output);
    if (!std::isfinite(current_gain_) || !std::isfinite(output)) {
        output = 0.0;
        return GenericVcaError::NonFinite;
    }
    return GenericVcaError::None;
}

void GenericVcaRuntime::Reset() noexcept {
    current_gain_ = TargetGain(model_, model_.initial_control_voltage);
}

} // namespace solo_rig
