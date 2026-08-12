#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace solo_rig {

struct TuningResult {
    double frequency_hz{};
    int midi_note{};
    double cents_from_nearest_note{};
    double confidence{};
};

class TunerAnalyzer {
public:
    [[nodiscard]] std::optional<TuningResult> Analyze(std::span<const float> mono_samples,
                                                       double sample_rate_hz,
                                                       double reference_pitch_hz = 440.0) const;
};

} // namespace solo_rig
