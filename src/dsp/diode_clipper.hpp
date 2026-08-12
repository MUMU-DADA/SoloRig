#pragma once

#include "core/audio_graph.hpp"

#include <cstdint>
#include <vector>

namespace solo_rig {

struct DiodeClipperSettings {
    float drive{1.0F};
    float output_gain{1.0F};
    float diode_threshold{0.35F};
    bool symmetrical{true};
    float input_resistance_ohms{4700.0F};
    float filter_capacitance_farads{0.0F};
    float diode_saturation_current{0.000000001F};
    float thermal_voltage{0.02585F};
    std::uint32_t newton_iterations{8};
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
    [[nodiscard]] float SolveSample(float input_sample, std::uint32_t channel) noexcept;

    DiodeClipperSettings settings_;
    double sample_rate_hz_{};
    std::vector<float> capacitor_voltage_;
};

} // namespace solo_rig
