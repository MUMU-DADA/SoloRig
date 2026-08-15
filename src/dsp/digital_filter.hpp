#pragma once

#include "core/audio_graph.hpp"

#include <cstdint>
#include <vector>

namespace solo_rig {

// A first-order low-pass operation for the external digital-algorithm
// contract. Cutoff is expressed in Hz and is bound by the operation's single
// parameter slot.
class DigitalFilter final : public EffectProcessor {
public:
    bool Prepare(const AudioFormat& format) override;
    void Process(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept override;
    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override { return 0U; }

    [[nodiscard]] bool SetCutoffHz(float cutoff_hz) noexcept;
    [[nodiscard]] float CutoffHz() const noexcept { return cutoff_hz_; }

private:
    float cutoff_hz_{1000.0F};
    double sample_rate_hz_{};
    std::uint32_t channel_count_{};
    std::vector<float> state_;
    bool prepared_{};
};

} // namespace solo_rig
