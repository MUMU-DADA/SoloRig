#pragma once

#include "core/audio_graph.hpp"

namespace solo_rig {

struct DiodeClipperSettings {
    float drive{1.0F};
    float output_gain{1.0F};
    float diode_threshold{0.35F};
    bool symmetrical{true};
};

class DiodeClipper final : public EffectProcessor {
public:
    explicit DiodeClipper(DiodeClipperSettings settings = {});

    bool Prepare(const AudioFormat& format) override;
    void Process(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept override;
    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override;

    [[nodiscard]] const DiodeClipperSettings& Settings() const noexcept;
    [[nodiscard]] bool SetSettings(DiodeClipperSettings settings) noexcept;

private:
    DiodeClipperSettings settings_;
};

} // namespace solo_rig
