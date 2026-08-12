#pragma once

#include <cstdint>
#include <vector>

namespace solo_rig {

class AudioBuffer {
public:
    AudioBuffer() = default;
    AudioBuffer(std::uint32_t channel_count, std::uint32_t capacity);

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] std::uint32_t ChannelCount() const noexcept;
    [[nodiscard]] std::uint32_t Capacity() const noexcept;

    [[nodiscard]] float* ChannelData(std::uint32_t channel) noexcept;
    [[nodiscard]] const float* ChannelData(std::uint32_t channel) const noexcept;

    void Clear(std::uint32_t sample_count) noexcept;
    [[nodiscard]] bool CopyFrom(const AudioBuffer& source, std::uint32_t sample_count) noexcept;
    [[nodiscard]] bool AddFrom(const AudioBuffer& source, std::uint32_t sample_count) noexcept;

private:
    std::uint32_t channel_count_{};
    std::uint32_t capacity_{};
    std::vector<float> samples_;
};

} // namespace solo_rig
