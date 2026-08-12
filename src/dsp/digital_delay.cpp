#include "dsp/digital_delay.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

namespace {

bool IsValid(const DigitalDelaySettings& settings, float maximum_delay_seconds) noexcept {
    return std::isfinite(settings.delay_seconds) && std::isfinite(settings.feedback) && std::isfinite(settings.wet) &&
           std::isfinite(settings.dry) && settings.delay_seconds >= 0.0F &&
           settings.delay_seconds <= maximum_delay_seconds && settings.feedback >= -0.99F &&
           settings.feedback <= 0.99F && settings.wet >= 0.0F && settings.wet <= 1.0F && settings.dry >= 0.0F &&
           settings.dry <= 1.0F;
}

} // namespace

DigitalDelay::DigitalDelay(float maximum_delay_seconds, DigitalDelaySettings settings)
    : maximum_delay_seconds_(maximum_delay_seconds), settings_(settings) {}

bool DigitalDelay::Prepare(const AudioFormat& format) {
    if (!format.Validate().empty() || !std::isfinite(maximum_delay_seconds_) || maximum_delay_seconds_ <= 0.0F ||
        !IsValid(settings_, maximum_delay_seconds_)) {
        return false;
    }

    sample_rate_hz_ = format.sample_rate_hz;
    channel_count_ = format.channel_count;
    ring_size_ = static_cast<std::uint32_t>(std::ceil(sample_rate_hz_ * maximum_delay_seconds_)) + 1U;
    delay_line_.assign(static_cast<std::size_t>(channel_count_) * ring_size_, 0.0F);
    write_position_ = 0;
    return true;
}

void DigitalDelay::Process(const AudioBuffer& input,
                           AudioBuffer& output,
                           std::uint32_t sample_count) noexcept {
    const auto delay_samples = DelaySamples();
    for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
        const auto read_position = (write_position_ + ring_size_ - delay_samples) % ring_size_;
        for (std::uint32_t channel = 0; channel < channel_count_; ++channel) {
            auto* delay_data = delay_line_.data() + static_cast<std::size_t>(channel) * ring_size_;
            const float delayed = delay_data[read_position];
            const float dry_input = input.ChannelData(channel)[sample];
            delay_data[write_position_] = dry_input + delayed * settings_.feedback;
            output.ChannelData(channel)[sample] = dry_input * settings_.dry + delayed * settings_.wet;
        }
        write_position_ = (write_position_ + 1U) % ring_size_;
    }
}

std::uint32_t DigitalDelay::DeclaredLatencySamples() const noexcept {
    return 0;
}

bool DigitalDelay::SetSettings(DigitalDelaySettings settings) noexcept {
    if (!IsValid(settings, maximum_delay_seconds_)) {
        return false;
    }
    settings_ = settings;
    return true;
}

const DigitalDelaySettings& DigitalDelay::Settings() const noexcept {
    return settings_;
}

std::uint32_t DigitalDelay::DelaySamples() const noexcept {
    return std::min(static_cast<std::uint32_t>(std::lround(settings_.delay_seconds * sample_rate_hz_)), ring_size_ - 1U);
}

} // namespace solo_rig
