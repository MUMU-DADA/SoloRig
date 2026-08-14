#pragma once

#include <optional>

namespace solo_rig {

struct CoupledInductorModel {
    double turns_ratio{1.0};
    double magnetizing_inductance{1.0};
    double primary_leakage_inductance{};
    double secondary_leakage_inductance{};
    double primary_resistance{};
    double secondary_resistance{};
};

struct CoupledInductorCoefficients {
    double primary_inductance{};
    double secondary_inductance{};
    double mutual_inductance{};
};

[[nodiscard]] bool ValidateCoupledInductorModel(const CoupledInductorModel& model) noexcept;
[[nodiscard]] std::optional<CoupledInductorCoefficients>
PrepareCoupledInductor(const CoupledInductorModel& model) noexcept;
[[nodiscard]] double CoupledInductorStoredEnergy(const CoupledInductorCoefficients& coefficients,
                                                 double primary_current,
                                                 double secondary_current) noexcept;

} // namespace solo_rig
