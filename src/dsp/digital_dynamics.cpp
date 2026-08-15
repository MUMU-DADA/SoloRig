#include "dsp/digital_dynamics.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

bool DigitalDynamics::Prepare(const AudioFormat& format) {
    if (!format.Validate().empty() || !std::isfinite(amount_) || amount_ < 0.0F || amount_ > 1.0F) {
        prepared_ = false;
        return false;
    }
    channel_count_ = format.channel_count;
    envelope_.assign(channel_count_, 0.0F);
    prepared_ = true;
    return true;
}

void DigitalDynamics::Process(const AudioBuffer& input,
                              AudioBuffer& output,
                              std::uint32_t sample_count) noexcept {
    if (!prepared_ || input.ChannelCount() != channel_count_ || output.ChannelCount() != channel_count_ ||
        sample_count > input.Capacity() || sample_count > output.Capacity()) {
        return;
    }
    const float threshold = 1.0F - 0.9F * amount_;
    const float ratio = 1.0F + 31.0F * amount_;
    const float attack = 0.35F;
    const float release = 0.995F;
    for (std::uint32_t channel = 0; channel < channel_count_; ++channel) {
        const auto* source = input.ChannelData(channel);
        auto* destination = output.ChannelData(channel);
        auto envelope = envelope_[channel];
        for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
            const float magnitude = std::fabs(source[sample]);
            const float coefficient = magnitude > envelope ? attack : release;
            envelope = coefficient * envelope + (1.0F - coefficient) * magnitude;
            float gain = 1.0F;
            if (envelope > threshold && envelope > 0.0F) {
                const float compressed = threshold + (envelope - threshold) / ratio;
                gain = std::min(1.0F, compressed / envelope);
            }
            destination[sample] = source[sample] * gain;
        }
        envelope_[channel] = envelope;
    }
}

bool DigitalDynamics::SetAmount(float amount) noexcept {
    if (!std::isfinite(amount) || amount < 0.0F || amount > 1.0F) {
        return false;
    }
    amount_ = amount;
    return true;
}

} // namespace solo_rig
