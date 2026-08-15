#include "dsp/generic_bbd_feedback.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

namespace {

constexpr std::uint32_t kMaximumOversampling = 16U;
constexpr std::uint32_t kMaximumRingSamples = 1U << 20U;
constexpr double kMaximumMagnitude = 1.0e7;

} // namespace

bool ValidateGenericBbdFeedbackModel(const GenericBbdFeedbackModel& model) noexcept {
    const double values[] = {model.clock_frequency_hz, model.minimum_clock_frequency_hz,
        model.maximum_clock_frequency_hz, model.clock_control_octaves,
        model.transfer_efficiency, model.feedback_gain, model.wet, model.dry,
        model.maximum_input};
    for (const double value : values) {
        if (!std::isfinite(value)) return false;
    }
    return model.bucket_count >= 1U && model.bucket_count <= 4096U &&
        model.minimum_clock_frequency_hz >= 1.0 &&
        model.minimum_clock_frequency_hz <= model.clock_frequency_hz &&
        model.clock_frequency_hz <= model.maximum_clock_frequency_hz &&
        model.maximum_clock_frequency_hz <= kMaximumMagnitude &&
        model.clock_control_octaves > 0.0 && model.clock_control_octaves <= 32.0 &&
        model.transfer_efficiency >= 0.0 && model.transfer_efficiency <= 1.0 &&
        model.feedback_gain >= -0.99 && model.feedback_gain <= 0.99 &&
        model.wet >= 0.0 && model.wet <= 1.0 && model.dry >= 0.0 && model.dry <= 1.0 &&
        model.maximum_input > 0.0 && model.maximum_input <= kMaximumMagnitude;
}

GenericBbdFeedbackError GenericBbdFeedbackRuntime::ConfigureStorage(
    double sample_rate_hz) noexcept {
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return GenericBbdFeedbackError::InvalidSampleRate;
    }
    const double processing_rate = sample_rate_hz * static_cast<double>(oversampling_);
    const double maximum_delay_seconds = static_cast<double>(model_.bucket_count) /
        (2.0 * model_.minimum_clock_frequency_hz);
    const double maximum_delay_samples = maximum_delay_seconds * processing_rate;
    if (!std::isfinite(processing_rate) || processing_rate <= 0.0 ||
        !std::isfinite(maximum_delay_samples) || maximum_delay_samples < 0.0 ||
        maximum_delay_samples > static_cast<double>(kMaximumRingSamples - 2U)) {
        return GenericBbdFeedbackError::InvalidSampleRate;
    }
    const auto required = static_cast<std::uint64_t>(std::ceil(maximum_delay_samples)) + 2U;
    if (required < 2U || required > kMaximumRingSamples) {
        return GenericBbdFeedbackError::InvalidSampleRate;
    }
    try {
        delay_line_.assign(static_cast<std::size_t>(required), 0.0);
    } catch (...) {
        return GenericBbdFeedbackError::InvalidSampleRate;
    }
    efficiency_power_ = std::pow(model_.transfer_efficiency,
        static_cast<double>(model_.bucket_count));
    if (!std::isfinite(efficiency_power_)) return GenericBbdFeedbackError::InvalidModel;
    sample_rate_hz_ = sample_rate_hz;
    processing_rate_hz_ = processing_rate;
    write_position_ = 0U;
    return GenericBbdFeedbackError::None;
}

GenericBbdFeedbackError GenericBbdFeedbackRuntime::Prepare(
    const GenericBbdFeedbackModel& model, double sample_rate_hz,
    std::uint32_t oversampling) noexcept {
    prepared_ = false;
    if (!ValidateGenericBbdFeedbackModel(model)) {
        return GenericBbdFeedbackError::InvalidModel;
    }
    if (oversampling == 0U || oversampling > kMaximumOversampling) {
        return GenericBbdFeedbackError::InvalidOversampling;
    }
    model_ = model;
    oversampling_ = oversampling;
    const auto result = ConfigureStorage(sample_rate_hz);
    if (result != GenericBbdFeedbackError::None) return result;
    prepared_ = true;
    Reset();
    return GenericBbdFeedbackError::None;
}

GenericBbdFeedbackError GenericBbdFeedbackRuntime::SetSampleRate(
    double sample_rate_hz) noexcept {
    if (!prepared_) return GenericBbdFeedbackError::NotPrepared;
    const auto result = ConfigureStorage(sample_rate_hz);
    if (result != GenericBbdFeedbackError::None) return result;
    Reset();
    return GenericBbdFeedbackError::None;
}

GenericBbdFeedbackError GenericBbdFeedbackRuntime::Process(
    double input, double clock_control, double& output) noexcept {
    output = 0.0;
    if (!prepared_ || delay_line_.size() < 2U) return GenericBbdFeedbackError::NotPrepared;
    if (!std::isfinite(input) || !std::isfinite(clock_control)) {
        return GenericBbdFeedbackError::NonFinite;
    }
    const double bounded_input = std::clamp(input, -model_.maximum_input,
        model_.maximum_input);
    const double bounded_control = std::clamp(clock_control, -32.0, 32.0);
    const double frequency = std::clamp(model_.clock_frequency_hz *
        std::exp2(bounded_control * model_.clock_control_octaves),
        model_.minimum_clock_frequency_hz, model_.maximum_clock_frequency_hz);
    if (!std::isfinite(frequency) || frequency <= 0.0) {
        return GenericBbdFeedbackError::NonFinite;
    }
    current_clock_frequency_hz_ = frequency;
    delay_samples_ = static_cast<double>(model_.bucket_count) * processing_rate_hz_ /
        (2.0 * frequency);
    if (!std::isfinite(delay_samples_) || delay_samples_ < 0.0 ||
        delay_samples_ > static_cast<double>(delay_line_.size() - 2U)) {
        return GenericBbdFeedbackError::NonFinite;
    }
    const auto size = static_cast<std::uint32_t>(delay_line_.size());
    for (std::uint32_t substep = 0U; substep < oversampling_; ++substep) {
        double read_position = static_cast<double>(write_position_) - delay_samples_;
        while (read_position < 0.0) read_position += static_cast<double>(size);
        while (read_position >= static_cast<double>(size)) {
            read_position -= static_cast<double>(size);
        }
        const auto first = static_cast<std::uint32_t>(read_position);
        const auto second = (first + 1U) % size;
        const double fraction = read_position - static_cast<double>(first);
        const double delayed = (1.0 - fraction) * delay_line_[first] +
            fraction * delay_line_[second];
        const double write_value = bounded_input + model_.feedback_gain * previous_output_;
        const double next_output = model_.dry * bounded_input +
            model_.wet * efficiency_power_ * delayed;
        if (!std::isfinite(write_value) || !std::isfinite(next_output)) {
            output = 0.0;
            return GenericBbdFeedbackError::NonFinite;
        }
        delay_line_[write_position_] = write_value;
        write_position_ = (write_position_ + 1U) % size;
        output = next_output;
        previous_output_ = next_output;
    }
    return GenericBbdFeedbackError::None;
}

void GenericBbdFeedbackRuntime::Reset() noexcept {
    std::fill(delay_line_.begin(), delay_line_.end(), 0.0);
    write_position_ = 0U;
    previous_output_ = 0.0;
    current_clock_frequency_hz_ = model_.clock_frequency_hz;
    delay_samples_ = processing_rate_hz_ * static_cast<double>(model_.bucket_count) /
        (2.0 * model_.clock_frequency_hz);
}

} // namespace solo_rig
