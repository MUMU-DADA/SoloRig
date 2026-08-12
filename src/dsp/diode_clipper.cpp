#include "dsp/diode_clipper.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

namespace {

bool IsValid(const DiodeClipperSettings& settings) noexcept {
    return std::isfinite(settings.drive) && std::isfinite(settings.output_gain) &&
           std::isfinite(settings.diode_threshold) && settings.drive >= 0.0F && settings.output_gain >= 0.0F &&
           settings.diode_threshold > 0.0F;
}

float Clip(float sample, const DiodeClipperSettings& settings) noexcept {
    const float driven = sample * settings.drive;
    const float threshold = settings.diode_threshold;
    const float magnitude = std::fabs(driven);
    if (magnitude <= threshold) {
        return driven * settings.output_gain;
    }

    // A continuously differentiable, diode-like soft limiter that is stable in a real-time sample loop.
    const float clipped_magnitude = threshold + std::tanh(magnitude - threshold) * threshold;
    const float polarity = driven < 0.0F && settings.symmetrical ? -1.0F : 1.0F;
    const float asymmetrical_magnitude = driven < 0.0F && !settings.symmetrical ?
                                           magnitude : clipped_magnitude;
    return polarity * asymmetrical_magnitude * settings.output_gain;
}

} // namespace

DiodeClipper::DiodeClipper(DiodeClipperSettings settings) : settings_(settings) {}

bool DiodeClipper::Prepare(const AudioFormat& format) {
    return format.Validate().empty() && IsValid(settings_);
}

void DiodeClipper::Process(const AudioBuffer& input,
                           AudioBuffer& output,
                           std::uint32_t sample_count) noexcept {
    for (std::uint32_t channel = 0; channel < input.ChannelCount(); ++channel) {
        const auto* input_data = input.ChannelData(channel);
        auto* output_data = output.ChannelData(channel);
        for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
            output_data[sample] = Clip(input_data[sample], settings_);
        }
    }
}

std::uint32_t DiodeClipper::DeclaredLatencySamples() const noexcept {
    return 0;
}

const DiodeClipperSettings& DiodeClipper::Settings() const noexcept {
    return settings_;
}

bool DiodeClipper::SetSettings(DiodeClipperSettings settings) noexcept {
    if (!IsValid(settings)) {
        return false;
    }
    settings_ = settings;
    return true;
}

} // namespace solo_rig
