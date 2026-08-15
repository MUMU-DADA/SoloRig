#pragma once

#include "core/audio_graph.hpp"

#include <cstdint>
#include <vector>

namespace solo_rig {

// A fixed-capacity two-grain pitch shifter. The operation parameter is the
// requested shift in semitones in the inclusive range [-24, 24]. All grain
// storage is allocated by Prepare; Process only advances preallocated state.
class DigitalPitchShift final : public EffectProcessor {
public:
    bool Prepare(const AudioFormat& format) override;
    void Process(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept override;
    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override { return kGrainSize * 3U; }

    [[nodiscard]] bool SetSemitones(float semitones) noexcept;
    [[nodiscard]] float Semitones() const noexcept { return semitones_; }

private:
    struct Grain {
        double read_position{};
        std::uint32_t age{};
    };

    static constexpr std::uint32_t kGrainSize = 512U;
    static constexpr float kMinimumSemitones = -24.0F;
    static constexpr float kMaximumSemitones = 24.0F;

    [[nodiscard]] float Read(std::uint32_t channel, double position) const noexcept;
    void StartGrain(Grain& grain) noexcept;

    float semitones_{};
    double ratio_{1.0};
    double sample_rate_hz_{};
    std::uint32_t channel_count_{};
    std::uint32_t ring_size_{};
    std::uint32_t write_position_{};
    std::vector<float> ring_;
    std::vector<Grain> grains_a_;
    std::vector<Grain> grains_b_;
    bool prepared_{};
};

} // namespace solo_rig
