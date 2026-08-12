#pragma once

#include "core/audio_graph.hpp"

#include <cstdint>
#include <vector>

namespace solo_rig {

struct DigitalDelaySettings {
    float delay_seconds{0.35F};
    float feedback{0.35F};
    float wet{0.25F};
    float dry{1.0F};
};

class DigitalDelay final : public EffectProcessor {
public:
    explicit DigitalDelay(float maximum_delay_seconds = 2.0F, DigitalDelaySettings settings = {});

    bool Prepare(const AudioFormat& format) override;
    void Process(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept override;
    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override;

    [[nodiscard]] bool SetSettings(DigitalDelaySettings settings) noexcept;
    [[nodiscard]] const DigitalDelaySettings& Settings() const noexcept;

private:
    [[nodiscard]] std::uint32_t DelaySamples() const noexcept;

    float maximum_delay_seconds_;
    DigitalDelaySettings settings_;
    double sample_rate_hz_{};
    std::uint32_t channel_count_{};
    std::uint32_t ring_size_{};
    std::uint32_t write_position_{};
    std::vector<float> delay_line_;
};

} // namespace solo_rig
