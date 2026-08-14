#include "dsp/generic_bbd.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

namespace {

constexpr std::uint32_t kMaximumOversampling = 16U;
constexpr std::uint32_t kMaximumRingSamples = 1U << 20U;

} // namespace

bool ValidateGenericBbdModel(const GenericBbdModel& model) noexcept {
    const double values[] = {model.clock_frequency_hz, model.transfer_efficiency,
        model.feedback, model.wet, model.dry, model.maximum_input};
    for (const double value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return model.bucket_count >= 1U && model.bucket_count <= 4096U &&
           model.clock_frequency_hz >= 1.0 && model.clock_frequency_hz <= 1.0e7 &&
           model.transfer_efficiency >= 0.0 && model.transfer_efficiency <= 1.0 &&
           model.feedback >= -0.99 && model.feedback <= 0.99 && model.wet >= 0.0 &&
           model.wet <= 1.0 && model.dry >= 0.0 && model.dry <= 1.0 &&
           model.maximum_input > 0.0 && model.maximum_input <= 1.0e6;
}

GenericBbdError GenericBbdRuntime::ConfigureStorage(double sample_rate_hz) noexcept {
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return GenericBbdError::InvalidSampleRate;
    }
    const double processing_rate = sample_rate_hz * static_cast<double>(oversampling_);
    delay_seconds_ = static_cast<double>(model_.bucket_count) /
        (2.0 * model_.clock_frequency_hz);
    delay_samples_ = delay_seconds_ * processing_rate;
    if (!std::isfinite(delay_seconds_) || !std::isfinite(delay_samples_) || delay_samples_ < 0.0 ||
        delay_samples_ > static_cast<double>(kMaximumRingSamples - 2U)) {
        return GenericBbdError::InvalidSampleRate;
    }
    const auto required = static_cast<std::uint64_t>(std::ceil(delay_samples_)) + 2U;
    if (required < 2U || required > kMaximumRingSamples) {
        return GenericBbdError::InvalidSampleRate;
    }
    try {
        delay_line_.assign(static_cast<std::size_t>(required), 0.0);
    } catch (...) {
        return GenericBbdError::InvalidSampleRate;
    }
    efficiency_power_ = std::pow(model_.transfer_efficiency,
                                 static_cast<double>(model_.bucket_count));
    if (!std::isfinite(efficiency_power_)) {
        return GenericBbdError::InvalidModel;
    }
    write_position_ = 0U;
    sample_rate_hz_ = sample_rate_hz;
    return GenericBbdError::None;
}

GenericBbdError GenericBbdRuntime::Prepare(const GenericBbdModel& model,
                                           double sample_rate_hz,
                                           std::uint32_t oversampling) noexcept {
    if (!ValidateGenericBbdModel(model)) {
        return GenericBbdError::InvalidModel;
    }
    if (oversampling == 0U || oversampling > kMaximumOversampling) {
        return GenericBbdError::InvalidOversampling;
    }
    model_ = model;
    oversampling_ = oversampling;
    const auto result = ConfigureStorage(sample_rate_hz);
    prepared_ = result == GenericBbdError::None;
    if (prepared_) {
        Reset();
    }
    return result;
}

GenericBbdError GenericBbdRuntime::SetSampleRate(double sample_rate_hz) noexcept {
    if (!prepared_) {
        return GenericBbdError::NotPrepared;
    }
    const auto result = ConfigureStorage(sample_rate_hz);
    if (result == GenericBbdError::None) {
        Reset();
    }
    return result;
}

GenericBbdError GenericBbdRuntime::Process(double input, double& output) noexcept {
    output = 0.0;
    if (!prepared_ || delay_line_.size() < 2U) {
        return GenericBbdError::NotPrepared;
    }
    if (!std::isfinite(input)) {
        return GenericBbdError::NonFinite;
    }
    const double bounded_input = std::clamp(input, -model_.maximum_input, model_.maximum_input);
    const auto size = static_cast<std::uint32_t>(delay_line_.size());
    for (std::uint32_t substep = 0; substep < oversampling_; ++substep) {
        double read_position = static_cast<double>(write_position_) - delay_samples_;
        while (read_position < 0.0) {
            read_position += static_cast<double>(size);
        }
        while (read_position >= static_cast<double>(size)) {
            read_position -= static_cast<double>(size);
        }
        const auto first = static_cast<std::uint32_t>(read_position);
        const auto second = (first + 1U) % size;
        const double fraction = read_position - static_cast<double>(first);
        const double delayed = (1.0 - fraction) * delay_line_[first] + fraction * delay_line_[second];
        const double write_value = bounded_input + model_.feedback * delayed;
        delay_line_[write_position_] = write_value;
        write_position_ = (write_position_ + 1U) % size;
        output = model_.dry * bounded_input + model_.wet * efficiency_power_ * delayed;
        if (!std::isfinite(output) || !std::isfinite(write_value)) {
            return GenericBbdError::NonFinite;
        }
    }
    return GenericBbdError::None;
}

void GenericBbdRuntime::Reset() noexcept {
    std::fill(delay_line_.begin(), delay_line_.end(), 0.0);
    write_position_ = 0U;
}

} // namespace solo_rig
