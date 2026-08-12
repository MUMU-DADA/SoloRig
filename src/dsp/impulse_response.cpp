#include "dsp/impulse_response.hpp"

#include <cmath>

namespace solo_rig {

ImpulseResponseConvolver::ImpulseResponseConvolver(std::uint32_t maximum_ir_length)
    : maximum_ir_length_(maximum_ir_length) {}

std::vector<ImpulseResponseLoadError> ImpulseResponseConvolver::LoadMonoImpulseResponse(std::span<const float> samples) {
    std::vector<ImpulseResponseLoadError> errors;
    if (samples.empty()) {
        errors.push_back(ImpulseResponseLoadError::EmptyImpulseResponse);
    }
    if (samples.size() > maximum_ir_length_) {
        errors.push_back(ImpulseResponseLoadError::TooLong);
    }
    for (const auto sample : samples) {
        if (!std::isfinite(sample)) {
            errors.push_back(ImpulseResponseLoadError::NonFiniteSample);
            break;
        }
    }
    if (!errors.empty()) {
        return errors;
    }

    impulse_response_.assign(samples.begin(), samples.end());
    history_.clear();
    history_position_ = 0;
    return {};
}

bool ImpulseResponseConvolver::Prepare(const AudioFormat& format) {
    if (!format.Validate().empty() || impulse_response_.empty()) {
        return false;
    }
    channel_count_ = format.channel_count;
    history_.assign(static_cast<std::size_t>(channel_count_) * impulse_response_.size(), 0.0F);
    history_position_ = 0;
    return true;
}

void ImpulseResponseConvolver::Process(const AudioBuffer& input,
                                       AudioBuffer& output,
                                       std::uint32_t sample_count) noexcept {
    const auto ir_size = static_cast<std::uint32_t>(impulse_response_.size());
    for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
        for (std::uint32_t channel = 0; channel < channel_count_; ++channel) {
            auto* channel_history = history_.data() + static_cast<std::size_t>(channel) * ir_size;
            channel_history[history_position_] = input.ChannelData(channel)[sample];

            float accumulated = 0.0F;
            for (std::uint32_t tap = 0; tap < ir_size; ++tap) {
                const auto history_index = (history_position_ + ir_size - tap) % ir_size;
                accumulated += channel_history[history_index] * impulse_response_[tap];
            }
            output.ChannelData(channel)[sample] = accumulated;
        }
        history_position_ = (history_position_ + 1U) % ir_size;
    }
}

std::uint32_t ImpulseResponseConvolver::DeclaredLatencySamples() const noexcept {
    return 0;
}

std::uint32_t ImpulseResponseConvolver::ImpulseResponseLength() const noexcept {
    return static_cast<std::uint32_t>(impulse_response_.size());
}

} // namespace solo_rig
