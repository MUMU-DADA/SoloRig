#pragma once

#include "core/audio_graph.hpp"

#include <cstdint>
#include <vector>

namespace solo_rig {

// A bounded envelope compressor. Amount is normalized to [0, 1]: zero is a
// transparent operation and one is a hard, high-ratio limiter.
class DigitalDynamics final : public EffectProcessor {
public:
    bool Prepare(const AudioFormat& format) override;
    void Process(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept override;
    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override { return 0U; }

    [[nodiscard]] bool SetAmount(float amount) noexcept;
    [[nodiscard]] float Amount() const noexcept { return amount_; }

private:
    float amount_{};
    std::uint32_t channel_count_{};
    std::vector<float> envelope_;
    bool prepared_{};
};

} // namespace solo_rig
