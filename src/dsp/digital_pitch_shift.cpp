#include "dsp/digital_pitch_shift.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace solo_rig {

bool DigitalPitchShift::Prepare(const AudioFormat& format) {
    if (!format.Validate().empty() || !std::isfinite(semitones_) || semitones_ < kMinimumSemitones ||
        semitones_ > kMaximumSemitones) {
        prepared_ = false;
        return false;
    }
    sample_rate_hz_ = format.sample_rate_hz;
    channel_count_ = format.channel_count;
    // The second grain starts half a window earlier than the first one. The
    // extra capacity keeps that read head behind the write head even at the
    // maximum four-times resampling ratio.
    ring_size_ = kGrainSize * 8U + 4U;
    ring_.assign(static_cast<std::size_t>(channel_count_) * ring_size_, 0.0F);
    grains_a_.assign(channel_count_, {});
    grains_b_.assign(channel_count_, {});
    write_position_ = 0U;
    ratio_ = std::pow(2.0, static_cast<double>(semitones_) / 12.0);
    prepared_ = true;
    for (std::uint32_t channel = 0; channel < channel_count_; ++channel) {
        StartGrain(grains_a_[channel]);
        grains_b_[channel] = grains_a_[channel];
        grains_b_[channel].age = kGrainSize / 2U;
        grains_b_[channel].read_position -= ratio_ * static_cast<double>(kGrainSize / 2U);
    }
    return true;
}

void DigitalPitchShift::Process(const AudioBuffer& input,
                                AudioBuffer& output,
                                std::uint32_t sample_count) noexcept {
    if (!prepared_ || input.ChannelCount() != channel_count_ || output.ChannelCount() != channel_count_ ||
        sample_count > input.Capacity() || sample_count > output.Capacity()) {
        return;
    }
    constexpr double two_pi = 2.0 * 3.14159265358979323846;
    for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
        for (std::uint32_t channel = 0; channel < channel_count_; ++channel) {
            ring_[static_cast<std::size_t>(channel) * ring_size_ + write_position_] =
                input.ChannelData(channel)[sample];
            const auto render = [this, channel, two_pi](Grain& grain) noexcept {
                const double phase = static_cast<double>(grain.age) / static_cast<double>(kGrainSize - 1U);
                const float window = static_cast<float>(0.5 - 0.5 * std::cos(two_pi * phase));
                const float value = Read(channel, grain.read_position);
                grain.read_position += ratio_;
                ++grain.age;
                if (grain.age >= kGrainSize) {
                    StartGrain(grain);
                }
                return std::pair<float, float>{value * window, window};
            };
            const auto first = render(grains_a_[channel]);
            const auto second = render(grains_b_[channel]);
            const float weight = first.second + second.second;
            output.ChannelData(channel)[sample] = weight > 1.0e-6F
                ? (first.first + second.first) / weight : 0.0F;
        }
        write_position_ = (write_position_ + 1U) % ring_size_;
    }
}

bool DigitalPitchShift::SetSemitones(float semitones) noexcept {
    if (!std::isfinite(semitones) || semitones < kMinimumSemitones || semitones > kMaximumSemitones) {
        return false;
    }
    semitones_ = semitones;
    ratio_ = std::pow(2.0, static_cast<double>(semitones_) / 12.0);
    return true;
}

float DigitalPitchShift::Read(std::uint32_t channel, double position) const noexcept {
    while (position < 0.0) {
        position += ring_size_;
    }
    while (position >= ring_size_) {
        position -= ring_size_;
    }
    const auto left = static_cast<std::uint32_t>(position);
    const auto right = (left + 1U) % ring_size_;
    const float fraction = static_cast<float>(position - static_cast<double>(left));
    const auto* data = ring_.data() + static_cast<std::size_t>(channel) * ring_size_;
    return data[left] + fraction * (data[right] - data[left]);
}

void DigitalPitchShift::StartGrain(Grain& grain) noexcept {
    const double maximum_ratio = std::max(ratio_, 1.0 / ratio_);
    const double delay = static_cast<double>(kGrainSize) * (1.0 + maximum_ratio);
    grain.read_position = static_cast<double>(write_position_) - delay;
    while (grain.read_position < 0.0) {
        grain.read_position += ring_size_;
    }
    grain.age = 0U;
}

} // namespace solo_rig
