#include "dsp/generic_vco.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

namespace {

constexpr std::uint32_t kMaximumOversampling = 16U;
constexpr double kMaximumFrequencyHz = 1.0e6;
constexpr double kMaximumMagnitude = 1.0e6;
constexpr double kMaximumOctaves = 32.0;
constexpr double kTwoPi = 6.28318530717958647692;

bool IsKnownWaveform(GenericVcoWaveform waveform) noexcept {
    switch (waveform) {
    case GenericVcoWaveform::Sine:
    case GenericVcoWaveform::Triangle:
    case GenericVcoWaveform::Saw:
    case GenericVcoWaveform::Square:
        return true;
    }
    return false;
}

double WrapPhase(double phase_cycles) noexcept {
    return phase_cycles - std::floor(phase_cycles);
}

} // namespace

bool ValidateGenericVcoModel(const GenericVcoModel& model) noexcept {
    const double values[] = {model.base_frequency_hz, model.minimum_frequency_hz,
        model.maximum_frequency_hz, model.volts_per_octave, model.reference_voltage,
        model.output_amplitude, model.output_offset, model.initial_phase_cycles,
        model.maximum_control_voltage};
    for (const double value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return IsKnownWaveform(model.waveform) && model.minimum_frequency_hz > 0.0 &&
           model.minimum_frequency_hz <= model.base_frequency_hz &&
           model.base_frequency_hz <= model.maximum_frequency_hz &&
           model.maximum_frequency_hz <= kMaximumFrequencyHz &&
           model.volts_per_octave >= 1.0e-6 && model.volts_per_octave <= kMaximumMagnitude &&
           std::fabs(model.reference_voltage) <= kMaximumMagnitude &&
           model.output_amplitude >= 0.0 && model.output_amplitude <= kMaximumMagnitude &&
           std::fabs(model.output_offset) <= kMaximumMagnitude &&
           model.initial_phase_cycles >= 0.0 && model.initial_phase_cycles < 1.0 &&
           model.maximum_control_voltage >= 1.0e-6 &&
           model.maximum_control_voltage <= kMaximumMagnitude;
}

double EvaluateGenericVcoWaveform(GenericVcoWaveform waveform, double phase_cycles) noexcept {
    if (!std::isfinite(phase_cycles)) {
        return 0.0;
    }
    const double phase = WrapPhase(phase_cycles);
    switch (waveform) {
    case GenericVcoWaveform::Sine:
        return std::sin(kTwoPi * phase);
    case GenericVcoWaveform::Triangle:
        return 1.0 - 4.0 * std::fabs(phase - 0.5);
    case GenericVcoWaveform::Saw:
        return 2.0 * phase - 1.0;
    case GenericVcoWaveform::Square:
        return phase < 0.5 ? 1.0 : -1.0;
    }
    return 0.0;
}

GenericVcoError GenericVcoRuntime::ValidateRate(double sample_rate_hz,
                                                double& processing_rate_hz) const noexcept {
    processing_rate_hz = sample_rate_hz * static_cast<double>(oversampling_);
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0 ||
        !std::isfinite(processing_rate_hz) || processing_rate_hz <= 0.0 ||
        model_.maximum_frequency_hz >= 0.5 * processing_rate_hz) {
        return GenericVcoError::InvalidSampleRate;
    }
    return GenericVcoError::None;
}

GenericVcoError GenericVcoRuntime::Prepare(const GenericVcoModel& model,
                                           double sample_rate_hz,
                                           std::uint32_t oversampling) noexcept {
    prepared_ = false;
    if (!ValidateGenericVcoModel(model)) {
        return GenericVcoError::InvalidModel;
    }
    if (oversampling == 0U || oversampling > kMaximumOversampling) {
        return GenericVcoError::InvalidOversampling;
    }
    model_ = model;
    oversampling_ = oversampling;
    double processing_rate_hz{};
    const auto rate_result = ValidateRate(sample_rate_hz, processing_rate_hz);
    if (rate_result != GenericVcoError::None) {
        return rate_result;
    }
    sample_rate_hz_ = sample_rate_hz;
    processing_rate_hz_ = processing_rate_hz;
    prepared_ = true;
    Reset();
    return GenericVcoError::None;
}

GenericVcoError GenericVcoRuntime::SetSampleRate(double sample_rate_hz) noexcept {
    if (!prepared_) {
        return GenericVcoError::NotPrepared;
    }
    double processing_rate_hz{};
    const auto rate_result = ValidateRate(sample_rate_hz, processing_rate_hz);
    if (rate_result != GenericVcoError::None) {
        return rate_result;
    }
    sample_rate_hz_ = sample_rate_hz;
    processing_rate_hz_ = processing_rate_hz;
    Reset();
    return GenericVcoError::None;
}

GenericVcoError GenericVcoRuntime::Process(double control_voltage, double& output) noexcept {
    output = 0.0;
    if (!prepared_) {
        return GenericVcoError::NotPrepared;
    }
    if (!std::isfinite(control_voltage)) {
        return GenericVcoError::NonFinite;
    }
    const double bounded_control = std::clamp(control_voltage,
        -model_.maximum_control_voltage, model_.maximum_control_voltage);
    const double octaves = std::clamp(
        (bounded_control - model_.reference_voltage) / model_.volts_per_octave,
        -kMaximumOctaves, kMaximumOctaves);
    current_frequency_hz_ = std::clamp(model_.base_frequency_hz * std::exp2(octaves),
        model_.minimum_frequency_hz, model_.maximum_frequency_hz);
    if (!std::isfinite(current_frequency_hz_)) {
        return GenericVcoError::NonFinite;
    }
    const double phase_increment = current_frequency_hz_ / processing_rate_hz_;
    for (std::uint32_t substep = 0U; substep < oversampling_; ++substep) {
        phase_cycles_ = WrapPhase(phase_cycles_ + phase_increment);
        output = model_.output_offset + model_.output_amplitude *
            EvaluateGenericVcoWaveform(model_.waveform, phase_cycles_);
        if (!std::isfinite(phase_cycles_) || !std::isfinite(output)) {
            output = 0.0;
            return GenericVcoError::NonFinite;
        }
    }
    return GenericVcoError::None;
}

GenericVcoError GenericVcoRuntime::ProcessGenerator(double& output) noexcept {
    if (!prepared_) {
        output = 0.0;
        return GenericVcoError::NotPrepared;
    }
    return Process(model_.reference_voltage, output);
}

void GenericVcoRuntime::Reset() noexcept {
    phase_cycles_ = model_.initial_phase_cycles;
    current_frequency_hz_ = model_.base_frequency_hz;
}

} // namespace solo_rig
