#include "dsp/digital_filter.hpp"

#include <cmath>

namespace solo_rig {

bool DigitalFilter::Prepare(const AudioFormat& format) {
    if (!format.Validate().empty() || !std::isfinite(cutoff_hz_) || cutoff_hz_ <= 0.0F ||
        cutoff_hz_ >= static_cast<float>(format.sample_rate_hz * 0.5)) {
        prepared_ = false;
        return false;
    }
    sample_rate_hz_ = format.sample_rate_hz;
    channel_count_ = format.channel_count;
    state_.assign(channel_count_, 0.0F);
    prepared_ = true;
    return true;
}

void DigitalFilter::Process(const AudioBuffer& input,
                            AudioBuffer& output,
                            std::uint32_t sample_count) noexcept {
    if (!prepared_ || input.ChannelCount() != channel_count_ || output.ChannelCount() != channel_count_ ||
        sample_count > input.Capacity() || sample_count > output.Capacity()) {
        return;
    }
    const auto alpha = static_cast<float>(std::exp(-2.0 * 3.14159265358979323846 * cutoff_hz_ / sample_rate_hz_));
    const auto feed = 1.0F - alpha;
    for (std::uint32_t channel = 0; channel < channel_count_; ++channel) {
        const auto* source = input.ChannelData(channel);
        auto* destination = output.ChannelData(channel);
        auto state = state_[channel];
        for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
            state += feed * (source[sample] - state);
            destination[sample] = state;
        }
        state_[channel] = state;
    }
}

bool DigitalFilter::SetCutoffHz(float cutoff_hz) noexcept {
    if (!std::isfinite(cutoff_hz) || cutoff_hz <= 0.0F ||
        (prepared_ && cutoff_hz >= static_cast<float>(sample_rate_hz_ * 0.5))) {
        return false;
    }
    cutoff_hz_ = cutoff_hz;
    return true;
}

} // namespace solo_rig
