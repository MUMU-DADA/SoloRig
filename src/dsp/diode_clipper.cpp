#include "dsp/diode_clipper.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

namespace {

bool IsValid(const DiodeClipperSettings& settings) noexcept {
    return std::isfinite(settings.drive) && std::isfinite(settings.output_gain) &&
           std::isfinite(settings.diode_threshold) && std::isfinite(settings.input_resistance_ohms) &&
           std::isfinite(settings.filter_capacitance_farads) && std::isfinite(settings.diode_saturation_current) &&
           std::isfinite(settings.thermal_voltage) && settings.drive >= 0.0F && settings.output_gain >= 0.0F &&
           settings.diode_threshold > 0.0F && settings.input_resistance_ohms > 0.0F &&
           settings.filter_capacitance_farads >= 0.0F && settings.diode_saturation_current > 0.0F &&
           settings.thermal_voltage > 0.0F && settings.newton_iterations > 0U && settings.newton_iterations <= 32U;
}

double SafeExp(double value) noexcept {
    return std::exp(std::clamp(value, -60.0, 60.0));
}

} // namespace

DiodeClipper::DiodeClipper(DiodeClipperSettings settings) : settings_(settings) {}

bool DiodeClipper::Prepare(const AudioFormat& format) {
    if (!format.Validate().empty() || !IsValid(settings_)) {
        return false;
    }
    sample_rate_hz_ = format.sample_rate_hz;
    capacitor_voltage_.assign(format.channel_count, 0.0F);
    return true;
}

void DiodeClipper::Process(const AudioBuffer& input,
                           AudioBuffer& output,
                           std::uint32_t sample_count) noexcept {
    for (std::uint32_t channel = 0; channel < input.ChannelCount(); ++channel) {
        const auto* input_data = input.ChannelData(channel);
        auto* output_data = output.ChannelData(channel);
        for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
            output_data[sample] = SolveSample(input_data[sample], channel);
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

float DiodeClipper::SolveSample(float input_sample, std::uint32_t channel) noexcept {
    const auto input_voltage = static_cast<double>(input_sample) * settings_.drive;
    const auto resistance = static_cast<double>(settings_.input_resistance_ohms);
    const auto diode_threshold = static_cast<double>(settings_.diode_threshold);
    const auto saturation_current = static_cast<double>(settings_.diode_saturation_current);
    const auto thermal_voltage = static_cast<double>(settings_.thermal_voltage);
    const auto capacitor_conductance = static_cast<double>(settings_.filter_capacitance_farads) * sample_rate_hz_;
    const auto previous_voltage = static_cast<double>(capacitor_voltage_[channel]);
    const auto input_conductance = 1.0 / resistance;

    // This is a reduced modified-nodal-analysis equation for a voltage source
    // through Rin, a capacitor to ground (backward Euler), and one or two
    // nonlinear diode branches to ground. Start near the diode's analytical
    // operating region so bounded Newton iterations remain real-time stable.
    auto voltage = previous_voltage;
    const auto input_magnitude = std::fabs(input_voltage);
    if (input_magnitude > diode_threshold) {
        const auto diode_estimate = diode_threshold + thermal_voltage *
            std::log1p(input_magnitude * input_conductance / saturation_current);
        voltage = std::copysign(std::min(input_magnitude, diode_estimate), input_voltage);
    }

    const auto reverse_bias_current = SafeExp(-diode_threshold / thermal_voltage);
    for (std::uint32_t iteration = 0; iteration < settings_.newton_iterations; ++iteration) {
        const auto forward_exponential = SafeExp((voltage - diode_threshold) / thermal_voltage);
        const auto reverse_exponential = settings_.symmetrical
            ? SafeExp((-voltage - diode_threshold) / thermal_voltage)
            : reverse_bias_current;
        const auto diode_current = saturation_current * (forward_exponential - reverse_exponential);
        const auto diode_conductance = saturation_current / thermal_voltage *
            (forward_exponential + (settings_.symmetrical ? reverse_exponential : 0.0));
        const auto residual = input_conductance * (voltage - input_voltage) +
            capacitor_conductance * (voltage - previous_voltage) + diode_current;
        const auto derivative = input_conductance + capacitor_conductance + diode_conductance;
        if (!std::isfinite(residual) || !std::isfinite(derivative) || derivative <= 0.0) {
            voltage = 0.0;
            break;
        }
        const auto correction = residual / derivative;
        voltage -= std::clamp(correction, -1.0, 1.0);
        if (!std::isfinite(voltage)) {
            voltage = 0.0;
            break;
        }
        if (std::fabs(correction) < 0.000001) {
            break;
        }
    }

    capacitor_voltage_[channel] = static_cast<float>(voltage);
    return static_cast<float>(voltage * settings_.output_gain);
}

} // namespace solo_rig
