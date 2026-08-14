#include "dsp/coupled_inductor.hpp"

#include <cmath>
#include <limits>

namespace solo_rig {

namespace {

bool IsPositiveDefinite(const CoupledInductorCoefficients& coefficients) noexcept {
    if (!std::isfinite(coefficients.primary_inductance) ||
        !std::isfinite(coefficients.secondary_inductance) ||
        !std::isfinite(coefficients.mutual_inductance) ||
        coefficients.primary_inductance <= 0.0 || coefficients.secondary_inductance <= 0.0) {
        return false;
    }
    const double scale = std::sqrt(coefficients.primary_inductance) *
                         std::sqrt(coefficients.secondary_inductance);
    return std::isfinite(scale) && std::fabs(coefficients.mutual_inductance) < scale;
}

} // namespace

bool ValidateCoupledInductorModel(const CoupledInductorModel& model) noexcept {
    if (!std::isfinite(model.turns_ratio) || !std::isfinite(model.magnetizing_inductance) ||
        !std::isfinite(model.primary_leakage_inductance) ||
        !std::isfinite(model.secondary_leakage_inductance) ||
        !std::isfinite(model.primary_resistance) || !std::isfinite(model.secondary_resistance) ||
        model.turns_ratio < 1.0e-6 || model.turns_ratio > 1.0e6 ||
        model.magnetizing_inductance < 1.0e-12 || model.magnetizing_inductance > 1.0e6 ||
        model.primary_leakage_inductance < 0.0 || model.primary_leakage_inductance > 1.0e6 ||
        model.secondary_leakage_inductance < 0.0 || model.secondary_leakage_inductance > 1.0e6 ||
        model.primary_resistance < 0.0 || model.primary_resistance > 1.0e12 ||
        model.secondary_resistance < 0.0 || model.secondary_resistance > 1.0e12 ||
        (model.primary_leakage_inductance == 0.0 && model.secondary_leakage_inductance == 0.0)) {
        return false;
    }
    return PrepareCoupledInductor(model).has_value();
}

std::optional<CoupledInductorCoefficients>
PrepareCoupledInductor(const CoupledInductorModel& model) noexcept {
    if (!std::isfinite(model.turns_ratio) || !std::isfinite(model.magnetizing_inductance) ||
        !std::isfinite(model.primary_leakage_inductance) ||
        !std::isfinite(model.secondary_leakage_inductance) ||
        !std::isfinite(model.primary_resistance) || !std::isfinite(model.secondary_resistance) ||
        model.turns_ratio < 1.0e-6 || model.turns_ratio > 1.0e6 ||
        model.magnetizing_inductance < 1.0e-12 || model.magnetizing_inductance > 1.0e6 ||
        model.primary_leakage_inductance < 0.0 || model.primary_leakage_inductance > 1.0e6 ||
        model.secondary_leakage_inductance < 0.0 || model.secondary_leakage_inductance > 1.0e6 ||
        model.primary_resistance < 0.0 || model.primary_resistance > 1.0e12 ||
        model.secondary_resistance < 0.0 || model.secondary_resistance > 1.0e12 ||
        (model.primary_leakage_inductance == 0.0 && model.secondary_leakage_inductance == 0.0)) {
        return std::nullopt;
    }
    const double inverse_ratio = 1.0 / model.turns_ratio;
    const CoupledInductorCoefficients result{
        .primary_inductance = model.magnetizing_inductance + model.primary_leakage_inductance,
        .secondary_inductance = model.magnetizing_inductance * inverse_ratio * inverse_ratio +
                                model.secondary_leakage_inductance,
        .mutual_inductance = model.magnetizing_inductance * inverse_ratio};
    return IsPositiveDefinite(result) ? std::optional<CoupledInductorCoefficients>(result) : std::nullopt;
}

double CoupledInductorStoredEnergy(const CoupledInductorCoefficients& coefficients,
                                   double primary_current,
                                   double secondary_current) noexcept {
    if (!IsPositiveDefinite(coefficients) || !std::isfinite(primary_current) ||
        !std::isfinite(secondary_current)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double energy = 0.5 * (coefficients.primary_inductance * primary_current * primary_current +
        2.0 * coefficients.mutual_inductance * primary_current * secondary_current +
        coefficients.secondary_inductance * secondary_current * secondary_current);
    return std::isfinite(energy) && energy >= 0.0
        ? energy : std::numeric_limits<double>::quiet_NaN();
}

} // namespace solo_rig
