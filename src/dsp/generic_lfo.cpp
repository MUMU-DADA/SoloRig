#include "dsp/generic_lfo.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

namespace {

constexpr std::uint32_t kMaximumOversampling = 16U;
constexpr double kMinimumFrequencyHz = 1.0e-4;
constexpr double kMaximumFrequencyHz = 100.0;
constexpr double kMaximumMagnitude = 1.0e6;
constexpr double kTwoPi = 6.28318530717958647692;

bool IsKnownWaveform(GenericLfoWaveform waveform) noexcept {
    switch (waveform) {
    case GenericLfoWaveform::Sine:
    case GenericLfoWaveform::Triangle:
    case GenericLfoWaveform::Saw:
    case GenericLfoWaveform::Square:
        return true;
    }
    return false;
}

double WrapPhase(double phase_cycles) noexcept {
    return phase_cycles - std::floor(phase_cycles);
}

double WaveformValue(const GenericLfoModel& model, double phase_cycles) noexcept {
    const double phase = WrapPhase(phase_cycles);
    switch (model.waveform) {
    case GenericLfoWaveform::Sine:
        return std::sin(kTwoPi * phase);
    case GenericLfoWaveform::Triangle:
        return 1.0 - 4.0 * std::fabs(phase - 0.5);
    case GenericLfoWaveform::Saw:
        return 2.0 * phase - 1.0;
    case GenericLfoWaveform::Square:
        return phase < model.duty_cycle ? 1.0 : -1.0;
    }
    return 0.0;
}

double ScaledOutput(const GenericLfoModel& model, double phase_cycles) noexcept {
    return std::clamp(model.output_offset + model.output_amplitude *
        WaveformValue(model, phase_cycles), -model.maximum_output, model.maximum_output);
}

} // namespace

bool ValidateGenericLfoModel(const GenericLfoModel& model) noexcept {
    const double values[] = {model.frequency_hz, model.output_amplitude, model.output_offset,
        model.maximum_output, model.initial_phase_cycles, model.duty_cycle};
    for (const double value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return IsKnownWaveform(model.waveform) && model.frequency_hz >= kMinimumFrequencyHz &&
           model.frequency_hz <= kMaximumFrequencyHz && model.output_amplitude >= 0.0 &&
           model.output_amplitude <= kMaximumMagnitude &&
           std::fabs(model.output_offset) <= kMaximumMagnitude &&
           model.maximum_output >= 1.0e-6 && model.maximum_output <= kMaximumMagnitude &&
           model.initial_phase_cycles >= 0.0 && model.initial_phase_cycles < 1.0 &&
           model.duty_cycle >= 0.01 && model.duty_cycle <= 0.99;
}

GenericLfoError EvaluateGenericLfoOutput(const GenericLfoModel& model,
                                         double phase_cycles,
                                         double& output) noexcept {
    output = 0.0;
    if (!ValidateGenericLfoModel(model)) {
        return GenericLfoError::InvalidModel;
    }
    if (!std::isfinite(phase_cycles)) {
        return GenericLfoError::NonFinite;
    }
    output = ScaledOutput(model, phase_cycles);
    if (!std::isfinite(output)) {
        output = 0.0;
        return GenericLfoError::NonFinite;
    }
    return GenericLfoError::None;
}

GenericLfoError GenericLfoRuntime::ConfigureRate(double sample_rate_hz) noexcept {
    const double processing_rate_hz = sample_rate_hz * static_cast<double>(oversampling_);
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0 ||
        !std::isfinite(processing_rate_hz) || processing_rate_hz <= 0.0 ||
        model_.frequency_hz >= 0.5 * processing_rate_hz) {
        return GenericLfoError::InvalidSampleRate;
    }
    const double phase_increment = model_.frequency_hz / processing_rate_hz;
    if (!std::isfinite(phase_increment) || phase_increment <= 0.0 || phase_increment >= 0.5) {
        return GenericLfoError::InvalidSampleRate;
    }
    phase_increment_ = phase_increment;
    return GenericLfoError::None;
}

GenericLfoError GenericLfoRuntime::Prepare(const GenericLfoModel& model,
                                           double sample_rate_hz,
                                           std::uint32_t oversampling) noexcept {
    prepared_ = false;
    if (!ValidateGenericLfoModel(model)) {
        return GenericLfoError::InvalidModel;
    }
    if (oversampling == 0U || oversampling > kMaximumOversampling) {
        return GenericLfoError::InvalidOversampling;
    }
    model_ = model;
    oversampling_ = oversampling;
    const auto rate_result = ConfigureRate(sample_rate_hz);
    if (rate_result != GenericLfoError::None) {
        return rate_result;
    }
    prepared_ = true;
    Reset();
    return GenericLfoError::None;
}

GenericLfoError GenericLfoRuntime::SetSampleRate(double sample_rate_hz) noexcept {
    if (!prepared_) {
        return GenericLfoError::NotPrepared;
    }
    const auto rate_result = ConfigureRate(sample_rate_hz);
    if (rate_result != GenericLfoError::None) {
        return rate_result;
    }
    Reset();
    return GenericLfoError::None;
}

GenericLfoError GenericLfoRuntime::Process(double& output) noexcept {
    output = 0.0;
    if (!prepared_) {
        return GenericLfoError::NotPrepared;
    }
    for (std::uint32_t substep = 0U; substep < oversampling_; ++substep) {
        phase_cycles_ = WrapPhase(phase_cycles_ + phase_increment_);
        output = ScaledOutput(model_, phase_cycles_);
        if (!std::isfinite(phase_cycles_) || !std::isfinite(output)) {
            output = 0.0;
            return GenericLfoError::NonFinite;
        }
    }
    return GenericLfoError::None;
}

void GenericLfoRuntime::Reset() noexcept {
    phase_cycles_ = model_.initial_phase_cycles;
}

} // namespace solo_rig
