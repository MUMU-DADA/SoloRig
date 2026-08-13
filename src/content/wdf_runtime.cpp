#include "content/wdf_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace solo_rig {

namespace {

constexpr double kMinimumResistance = 1.0e-12;

bool FinitePositive(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

double ComputePortResistance(WdfElementKind kind,
                             double value,
                             double sample_rate_hz,
                             double turns_ratio) noexcept {
    switch (kind) {
    case WdfElementKind::Resistor:
        return value;
    case WdfElementKind::Capacitor:
        // Backward Euler is deliberately shared with the MNA reference path,
        // allowing the two independent runtimes to be compared sample for
        // sample at the M18-A linear boundary.
        return 1.0 / (value * sample_rate_hz);
    case WdfElementKind::Inductor:
        return value * sample_rate_hz;
    case WdfElementKind::Transformer:
        return value * turns_ratio * turns_ratio;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

} // namespace

double WdfPort::Current() const noexcept {
    return FinitePositive(resistance) && std::isfinite(incident) && std::isfinite(reflected)
        ? (incident - reflected) / (2.0 * resistance)
        : std::numeric_limits<double>::quiet_NaN();
}

WdfOnePortPrepareResult PrepareWdfOnePort(WdfElementKind kind,
                                          double value,
                                          double sample_rate_hz,
                                          double initial,
                                          double turns_ratio) {
    if (!FinitePositive(value) || !FinitePositive(sample_rate_hz) || !std::isfinite(initial) ||
        !FinitePositive(turns_ratio)) {
        return {.port = std::nullopt, .error = WdfError::InvalidValue};
    }
    WdfOnePort port;
    port.kind_ = kind;
    port.value_ = value;
    port.initial_ = initial;
    port.turns_ratio_ = turns_ratio;
    port.sample_rate_hz_ = sample_rate_hz;
    port.port_.resistance = ComputePortResistance(kind, value, sample_rate_hz, turns_ratio);
    if (!FinitePositive(port.port_.resistance) || port.port_.resistance < kMinimumResistance) {
        return {.port = std::nullopt, .error = WdfError::InvalidValue};
    }
    port.prepared_ = true;
    port.Reset();
    return {.port = std::move(port), .error = std::nullopt};
}

WdfError WdfOnePort::Process(double incident, double& reflected) noexcept {
    reflected = 0.0;
    if (!prepared_) {
        return WdfError::NotPrepared;
    }
    if (!std::isfinite(incident)) {
        return WdfError::NonFinite;
    }
    port_.incident = incident;
    reflected = ReflectedWave();
    switch (kind_) {
    case WdfElementKind::Resistor:
    case WdfElementKind::Transformer:
        // The port resistance is chosen equal to the element resistance, so
        // the matched one-port has zero reflection.
        break;
    case WdfElementKind::Capacitor:
        // With the backward-Euler port resistance, the reflected wave is the
        // previous capacitor voltage. Accepting an incident wave advances the
        // physical voltage without an iterative solve or allocation.
        state_ = (incident + reflected) * 0.5;
        break;
    case WdfElementKind::Inductor:
        // state_ stores R * previous_current, so the reflected wave is its
        // negative and remains valid when the physical current is preserved
        // across a sample-rate update.
        state_ = (incident - reflected) * 0.5;
        break;
    }
    if (!std::isfinite(reflected)) {
        return WdfError::NonFinite;
    }
    port_.reflected = reflected;
    return WdfError::None;
}

double WdfOnePort::ReflectedWave() const noexcept {
    if (!prepared_) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    switch (kind_) {
    case WdfElementKind::Resistor:
    case WdfElementKind::Transformer:
        return 0.0;
    case WdfElementKind::Capacitor:
        return state_;
    case WdfElementKind::Inductor:
        return -state_;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

void WdfOnePort::CopyPreparedState(const WdfOnePort& source) noexcept {
    kind_ = source.kind_;
    value_ = source.value_;
    initial_ = source.initial_;
    turns_ratio_ = source.turns_ratio_;
    sample_rate_hz_ = source.sample_rate_hz_;
    state_ = source.state_;
    port_ = source.port_;
    prepared_ = source.prepared_;
}

bool WdfOnePort::CanSetSampleRate(double sample_rate_hz) const noexcept {
    if (!prepared_ || !FinitePositive(sample_rate_hz)) {
        return false;
    }
    const double resistance = ComputePortResistance(kind_, value_, sample_rate_hz, turns_ratio_);
    return FinitePositive(resistance) && resistance >= kMinimumResistance;
}

bool WdfOnePort::SetSampleRate(double sample_rate_hz) noexcept {
    if (!CanSetSampleRate(sample_rate_hz)) {
        return false;
    }
    const double previous_resistance = port_.resistance;
    const double next_resistance = ComputePortResistance(kind_, value_, sample_rate_hz, turns_ratio_);
    if (kind_ == WdfElementKind::Inductor) {
        state_ *= next_resistance / previous_resistance;
    }
    port_.resistance = next_resistance;
    sample_rate_hz_ = sample_rate_hz;
    return true;
}

void WdfOnePort::Reset() noexcept {
    state_ = kind_ == WdfElementKind::Inductor ? port_.resistance * initial_ : initial_;
    port_.incident = 0.0;
    port_.reflected = 0.0;
}

WdfAdaptorPrepareResult PrepareWdfAdaptor(WdfAdaptorKind kind,
                                          std::span<const double> resistances,
                                          double turns_ratio) {
    if (resistances.size() < 2U || !FinitePositive(turns_ratio)) {
        return {.adaptor = std::nullopt, .error = WdfError::InvalidTopology};
    }
    if (kind == WdfAdaptorKind::RType && resistances.size() != 2U) {
        return {.adaptor = std::nullopt, .error = WdfError::InvalidTopology};
    }
    WdfAdaptor adaptor;
    adaptor.kind_ = kind;
    adaptor.turns_ratio_ = turns_ratio;
    adaptor.resistances_.assign(resistances.begin(), resistances.end());
    if (!std::all_of(adaptor.resistances_.begin(), adaptor.resistances_.end(), FinitePositive)) {
        return {.adaptor = std::nullopt, .error = WdfError::InvalidValue};
    }
    adaptor.prepared_ = true;
    return {.adaptor = std::move(adaptor), .error = std::nullopt};
}

WdfError WdfAdaptor::Scatter(std::span<const double> incident,
                             std::span<double> reflected) const noexcept {
    if (!prepared_ || incident.size() != resistances_.size() || reflected.size() != resistances_.size()) {
        return WdfError::NotPrepared;
    }
    if (!std::all_of(incident.begin(), incident.end(), [](double value) { return std::isfinite(value); })) {
        return WdfError::NonFinite;
    }
    if (kind_ == WdfAdaptorKind::Series) {
        double total_resistance = 0.0;
        double total_incident = 0.0;
        for (std::size_t index = 0; index < resistances_.size(); ++index) {
            total_resistance += resistances_[index];
            total_incident += incident[index];
        }
        if (!FinitePositive(total_resistance)) {
            return WdfError::InvalidValue;
        }
        const double current = total_incident / total_resistance;
        for (std::size_t index = 0; index < reflected.size(); ++index) {
            reflected[index] = incident[index] - 2.0 * resistances_[index] * current;
        }
    } else if (kind_ == WdfAdaptorKind::Parallel) {
        double total_conductance = 0.0;
        double weighted_incident = 0.0;
        for (std::size_t index = 0; index < resistances_.size(); ++index) {
            const double conductance = 1.0 / resistances_[index];
            total_conductance += conductance;
            weighted_incident += conductance * incident[index];
        }
        if (!FinitePositive(total_conductance)) {
            return WdfError::InvalidValue;
        }
        const double voltage = 2.0 * weighted_incident / total_conductance;
        for (std::size_t index = 0; index < reflected.size(); ++index) {
            reflected[index] = voltage - incident[index];
        }
    } else {
        const double first = resistances_[0];
        const double second = resistances_[1];
        const double reflected_second = turns_ratio_ * turns_ratio_ * second;
        const double denominator = first + reflected_second;
        if (!FinitePositive(denominator)) {
            return WdfError::InvalidValue;
        }
        // Lossless ideal-transformer scattering for v1 = n*v2 and
        // i2 = -n*i1. The secondary resistance is reflected by n^2.
        const double reflection = (reflected_second - first) / denominator;
        reflected[0] = reflection * incident[0] + (2.0 * first * turns_ratio_ / denominator) * incident[1];
        reflected[1] = (2.0 * second * turns_ratio_ / denominator) * incident[0] -
            reflection * incident[1];
    }
    return std::all_of(reflected.begin(), reflected.end(), [](double value) { return std::isfinite(value); })
        ? WdfError::None
        : WdfError::NonFinite;
}

bool WdfAdaptor::SetResistances(std::span<const double> resistances) noexcept {
    if (!prepared_ || resistances.size() != resistances_.size() ||
        !std::all_of(resistances.begin(), resistances.end(), FinitePositive)) {
        return false;
    }
    std::copy(resistances.begin(), resistances.end(), resistances_.begin());
    return true;
}

void WdfAdaptor::Reset() noexcept {
    // Adaptors are memoryless. Keeping an explicit reset makes the ownership
    // boundary uniform with stateful one-ports and future stateful adaptors.
}

} // namespace solo_rig
