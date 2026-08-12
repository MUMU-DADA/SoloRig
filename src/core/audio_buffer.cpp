#include "core/audio_buffer.hpp"

#include <algorithm>

namespace solo_rig {

AudioBuffer::AudioBuffer(std::uint32_t channel_count, std::uint32_t capacity)
    : channel_count_(channel_count), capacity_(capacity),
      samples_(static_cast<std::size_t>(channel_count) * static_cast<std::size_t>(capacity), 0.0F) {}

bool AudioBuffer::IsValid() const noexcept {
    return channel_count_ > 0 && capacity_ > 0 &&
           samples_.size() == static_cast<std::size_t>(channel_count_) * static_cast<std::size_t>(capacity_);
}

std::uint32_t AudioBuffer::ChannelCount() const noexcept {
    return channel_count_;
}

std::uint32_t AudioBuffer::Capacity() const noexcept {
    return capacity_;
}

float* AudioBuffer::ChannelData(std::uint32_t channel) noexcept {
    if (channel >= channel_count_) {
        return nullptr;
    }
    return samples_.data() + static_cast<std::size_t>(channel) * capacity_;
}

const float* AudioBuffer::ChannelData(std::uint32_t channel) const noexcept {
    if (channel >= channel_count_) {
        return nullptr;
    }
    return samples_.data() + static_cast<std::size_t>(channel) * capacity_;
}

void AudioBuffer::Clear(std::uint32_t sample_count) noexcept {
    if (sample_count > capacity_) {
        return;
    }
    for (std::uint32_t channel = 0; channel < channel_count_; ++channel) {
        std::fill_n(ChannelData(channel), sample_count, 0.0F);
    }
}

bool AudioBuffer::CopyFrom(const AudioBuffer& source, std::uint32_t sample_count) noexcept {
    if (channel_count_ != source.channel_count_ || sample_count > capacity_ || sample_count > source.capacity_) {
        return false;
    }
    for (std::uint32_t channel = 0; channel < channel_count_; ++channel) {
        std::copy_n(source.ChannelData(channel), sample_count, ChannelData(channel));
    }
    return true;
}

bool AudioBuffer::AddFrom(const AudioBuffer& source, std::uint32_t sample_count) noexcept {
    if (channel_count_ != source.channel_count_ || sample_count > capacity_ || sample_count > source.capacity_) {
        return false;
    }
    for (std::uint32_t channel = 0; channel < channel_count_; ++channel) {
        const auto* source_data = source.ChannelData(channel);
        auto* destination_data = ChannelData(channel);
        for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
            destination_data[sample] += source_data[sample];
        }
    }
    return true;
}

} // namespace solo_rig
