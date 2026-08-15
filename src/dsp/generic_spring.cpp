#include "dsp/generic_spring.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

namespace {

constexpr std::uint32_t kMaximumOversampling = 16U;
constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kMaximumMagnitude = 1.0e6;
constexpr double kMaximumFrequencyHz = 20000.0;

} // namespace

bool ValidateGenericSpringModel(const GenericSpringModel& model) noexcept {
    const double values[] = {model.base_frequency_hz, model.mode_spacing_hz,
        model.dispersion, model.decay_seconds, model.high_frequency_damping,
        model.drive_gain, model.pickup_gain, model.wet, model.dry,
        model.maximum_input, model.maximum_output};
    for (const double value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return model.mode_count >= 1U && model.mode_count <= kMaximumGenericSpringModes &&
           model.base_frequency_hz >= 1.0 &&
           model.base_frequency_hz <= kMaximumFrequencyHz &&
           model.mode_spacing_hz >= 0.0 &&
           model.mode_spacing_hz <= kMaximumFrequencyHz &&
           model.dispersion >= 0.0 && model.dispersion <= 4.0 &&
           model.decay_seconds >= 1.0e-3 && model.decay_seconds <= 30.0 &&
           model.high_frequency_damping >= 0.0 &&
           model.high_frequency_damping <= 100.0 &&
           model.drive_gain >= 0.0 && model.drive_gain <= kMaximumMagnitude &&
           model.pickup_gain >= 0.0 && model.pickup_gain <= kMaximumMagnitude &&
           model.wet >= 0.0 && model.wet <= 1.0 &&
           model.dry >= 0.0 && model.dry <= 1.0 &&
           model.maximum_input >= 1.0e-6 && model.maximum_input <= kMaximumMagnitude &&
           model.maximum_output >= 1.0e-6 && model.maximum_output <= kMaximumMagnitude;
}

GenericSpringError EvaluateGenericSpringMode(const GenericSpringModel& model,
                                             std::uint32_t mode_index,
                                             double processing_rate_hz,
                                             GenericSpringMode& output) noexcept {
    output = {};
    if (!ValidateGenericSpringModel(model)) {
        return GenericSpringError::InvalidModel;
    }
    if (mode_index >= model.mode_count) {
        return GenericSpringError::InvalidMode;
    }
    if (!std::isfinite(processing_rate_hz) || processing_rate_hz <= 0.0) {
        return GenericSpringError::InvalidSampleRate;
    }
    const double normalized_index = model.mode_count == 1U ? 0.0 :
        static_cast<double>(mode_index) / static_cast<double>(model.mode_count - 1U);
    const double frequency_hz = model.base_frequency_hz + model.mode_spacing_hz *
        static_cast<double>(mode_index) * (1.0 + model.dispersion * normalized_index);
    const double decay_seconds = model.decay_seconds /
        (1.0 + model.high_frequency_damping * normalized_index * normalized_index);
    if (!std::isfinite(frequency_hz) || !std::isfinite(decay_seconds) ||
        frequency_hz <= 0.0 || frequency_hz >= 0.45 * processing_rate_hz ||
        decay_seconds <= 0.0) {
        return GenericSpringError::InvalidSampleRate;
    }
    const double pole_radius = std::exp(-1.0 / (decay_seconds * processing_rate_hz));
    const double angle = kTwoPi * frequency_hz / processing_rate_hz;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    const double input_coefficient = 1.0 - pole_radius;
    if (!std::isfinite(pole_radius) || pole_radius <= 0.0 || pole_radius >= 1.0 ||
        !std::isfinite(cosine) || !std::isfinite(sine) ||
        !std::isfinite(input_coefficient) || input_coefficient <= 0.0) {
        return GenericSpringError::InvalidSampleRate;
    }
    output = {frequency_hz, decay_seconds, pole_radius, cosine, sine, input_coefficient};
    return GenericSpringError::None;
}

GenericSpringError GenericSpringRuntime::ConfigureRate(double sample_rate_hz) noexcept {
    const double processing_rate_hz = sample_rate_hz * static_cast<double>(oversampling_);
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0 ||
        !std::isfinite(processing_rate_hz) || processing_rate_hz <= 0.0) {
        return GenericSpringError::InvalidSampleRate;
    }
    std::array<GenericSpringMode, kMaximumGenericSpringModes> configured{};
    for (std::uint32_t index = 0U; index < model_.mode_count; ++index) {
        const auto result = EvaluateGenericSpringMode(
            model_, index, processing_rate_hz, configured[index]);
        if (result != GenericSpringError::None) {
            return result;
        }
    }
    modes_ = configured;
    return GenericSpringError::None;
}

GenericSpringError GenericSpringRuntime::Prepare(const GenericSpringModel& model,
                                                 double sample_rate_hz,
                                                 std::uint32_t oversampling) noexcept {
    prepared_ = false;
    if (!ValidateGenericSpringModel(model)) {
        return GenericSpringError::InvalidModel;
    }
    if (oversampling == 0U || oversampling > kMaximumOversampling) {
        return GenericSpringError::InvalidOversampling;
    }
    model_ = model;
    oversampling_ = oversampling;
    const auto rate_result = ConfigureRate(sample_rate_hz);
    if (rate_result != GenericSpringError::None) {
        return rate_result;
    }
    prepared_ = true;
    Reset();
    return GenericSpringError::None;
}

GenericSpringError GenericSpringRuntime::SetSampleRate(double sample_rate_hz) noexcept {
    if (!prepared_) {
        return GenericSpringError::NotPrepared;
    }
    const auto rate_result = ConfigureRate(sample_rate_hz);
    if (rate_result != GenericSpringError::None) {
        return rate_result;
    }
    Reset();
    return GenericSpringError::None;
}

GenericSpringError GenericSpringRuntime::Process(double input, double& output) noexcept {
    output = 0.0;
    if (!prepared_) {
        return GenericSpringError::NotPrepared;
    }
    if (!std::isfinite(input)) {
        return GenericSpringError::NonFinite;
    }
    const double bounded_input = std::clamp(input, -model_.maximum_input, model_.maximum_input);
    const double excitation = model_.drive_gain * bounded_input;
    for (std::uint32_t substep = 0U; substep < oversampling_; ++substep) {
        double modal_sum = 0.0;
        for (std::uint32_t index = 0U; index < model_.mode_count; ++index) {
            const auto& mode = modes_[index];
            const double old_real = real_state_[index];
            const double old_imaginary = imaginary_state_[index];
            const double next_real = mode.pole_radius *
                (mode.cosine * old_real - mode.sine * old_imaginary) +
                mode.input_coefficient * excitation;
            const double next_imaginary = mode.pole_radius *
                (mode.sine * old_real + mode.cosine * old_imaginary);
            if (!std::isfinite(next_real) || !std::isfinite(next_imaginary)) {
                Reset();
                return GenericSpringError::NonFinite;
            }
            real_state_[index] = next_real;
            imaginary_state_[index] = next_imaginary;
            modal_sum += next_real;
        }
        const double wet_signal = model_.pickup_gain *
            modal_sum / static_cast<double>(model_.mode_count);
        const double mixed = model_.dry * bounded_input + model_.wet * wet_signal;
        output = std::clamp(mixed, -model_.maximum_output, model_.maximum_output);
        if (!std::isfinite(modal_sum) || !std::isfinite(wet_signal) ||
            !std::isfinite(mixed) || !std::isfinite(output)) {
            output = 0.0;
            Reset();
            return GenericSpringError::NonFinite;
        }
    }
    return GenericSpringError::None;
}

void GenericSpringRuntime::Reset() noexcept {
    real_state_.fill(0.0);
    imaginary_state_.fill(0.0);
}

double GenericSpringRuntime::CurrentStateEnergy() const noexcept {
    double energy = 0.0;
    for (std::uint32_t index = 0U; index < model_.mode_count; ++index) {
        energy += real_state_[index] * real_state_[index] +
            imaginary_state_[index] * imaginary_state_[index];
    }
    return energy / static_cast<double>(model_.mode_count);
}

} // namespace solo_rig
