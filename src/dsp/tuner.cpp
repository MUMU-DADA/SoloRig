#include "dsp/tuner.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace solo_rig {

std::optional<TuningResult> TunerAnalyzer::Analyze(std::span<const float> mono_samples,
                                                    double sample_rate_hz,
                                                    double reference_pitch_hz) const {
    constexpr double minimum_frequency_hz = 50.0;
    constexpr double maximum_frequency_hz = 1200.0;
    constexpr float minimum_peak = 0.002F;

    if (mono_samples.size() < 32 || !std::isfinite(sample_rate_hz) || !std::isfinite(reference_pitch_hz) ||
        sample_rate_hz <= 0.0 || reference_pitch_hz <= 0.0) {
        return std::nullopt;
    }

    float peak = 0.0F;
    for (const auto sample : mono_samples) {
        if (!std::isfinite(sample)) {
            return std::nullopt;
        }
        peak = std::max(peak, std::fabs(sample));
    }
    if (peak < minimum_peak) {
        return std::nullopt;
    }

    const auto minimum_lag = static_cast<std::uint32_t>(std::floor(sample_rate_hz / maximum_frequency_hz));
    const auto maximum_lag = std::min<std::uint32_t>(
        static_cast<std::uint32_t>(std::ceil(sample_rate_hz / minimum_frequency_hz)),
        static_cast<std::uint32_t>(mono_samples.size() / 2U));
    if (minimum_lag < 1U || minimum_lag >= maximum_lag) {
        return std::nullopt;
    }

    double best_correlation = -1.0;
    std::uint32_t best_lag = 0;
    for (std::uint32_t lag = minimum_lag; lag <= maximum_lag; ++lag) {
        double dot_product = 0.0;
        double energy_a = 0.0;
        double energy_b = 0.0;
        for (std::size_t index = lag; index < mono_samples.size(); ++index) {
            const auto a = static_cast<double>(mono_samples[index]);
            const auto b = static_cast<double>(mono_samples[index - lag]);
            dot_product += a * b;
            energy_a += a * a;
            energy_b += b * b;
        }
        const auto denominator = std::sqrt(energy_a * energy_b);
        const auto correlation = denominator > 0.0 ? dot_product / denominator : -1.0;
        if (correlation > best_correlation) {
            best_correlation = correlation;
            best_lag = lag;
        }
    }

    if (best_lag == 0 || best_correlation < 0.6) {
        return std::nullopt;
    }

    const double frequency_hz = sample_rate_hz / static_cast<double>(best_lag);
    const double note_value = 69.0 + 12.0 * std::log2(frequency_hz / reference_pitch_hz);
    const int midi_note = static_cast<int>(std::lround(note_value));
    const double cents = (note_value - static_cast<double>(midi_note)) * 100.0;
    return TuningResult{
        .frequency_hz = frequency_hz,
        .midi_note = midi_note,
        .cents_from_nearest_note = cents,
        .confidence = best_correlation,
    };
}

} // namespace solo_rig
