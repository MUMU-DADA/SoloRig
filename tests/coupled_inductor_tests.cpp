#include "dsp/coupled_inductor.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>

TEST_CASE("M18-C8a coupled-inductor prepares exact lossy-linear coefficients") {
    const solo_rig::CoupledInductorModel model{
        .turns_ratio = 2.0,
        .magnetizing_inductance = 1.0,
        .primary_leakage_inductance = 0.1,
        .secondary_leakage_inductance = 0.025,
        .primary_resistance = 10.0,
        .secondary_resistance = 5.0};
    REQUIRE(solo_rig::ValidateCoupledInductorModel(model));
    const auto coefficients = solo_rig::PrepareCoupledInductor(model);
    REQUIRE(coefficients.has_value());
    REQUIRE(std::fabs(coefficients->primary_inductance - 1.1) < 1.0e-15);
    REQUIRE(std::fabs(coefficients->secondary_inductance - 0.275) < 1.0e-15);
    REQUIRE(std::fabs(coefficients->mutual_inductance - 0.5) < 1.0e-15);
    REQUIRE(coefficients->primary_inductance * coefficients->secondary_inductance -
            coefficients->mutual_inductance * coefficients->mutual_inductance > 0.0);
}

TEST_CASE("M18-C8a coupled-inductor stored energy matches the independent quadratic form") {
    const auto coefficients = *solo_rig::PrepareCoupledInductor({2.0, 1.0, 0.1, 0.025, 10.0, 5.0});
    for (const auto currents : {std::pair{0.25, -0.1}, std::pair{-0.4, 0.2}, std::pair{0.0, 0.0}}) {
        const double expected = 0.5 * (1.1 * currents.first * currents.first +
            currents.first * currents.second + 0.275 * currents.second * currents.second);
        const double energy = solo_rig::CoupledInductorStoredEnergy(
            coefficients, currents.first, currents.second);
        REQUIRE(std::isfinite(energy));
        REQUIRE(energy >= 0.0);
        REQUIRE(std::fabs(energy - expected) < 1.0e-15);
    }
}

TEST_CASE("M18-C8a coupled-inductor rejects singular extreme and non-finite models") {
    auto model = solo_rig::CoupledInductorModel{2.0, 1.0, 0.1, 0.025, 10.0, 5.0};
    model.primary_leakage_inductance = 0.0;
    model.secondary_leakage_inductance = 0.0;
    REQUIRE(!solo_rig::ValidateCoupledInductorModel(model));
    model = {1.0e-6, 1.0e6, 1.0e6, 1.0e6, 1.0e12, 1.0e12};
    REQUIRE(solo_rig::ValidateCoupledInductorModel(model));
    REQUIRE(solo_rig::PrepareCoupledInductor(model).has_value());
    model.turns_ratio = std::numeric_limits<double>::infinity();
    REQUIRE(!solo_rig::ValidateCoupledInductorModel(model));
    model = {2.0, 1.0, 0.1, 0.025, 10.0, 5.0};
    REQUIRE(std::isnan(solo_rig::CoupledInductorStoredEnergy(
        *solo_rig::PrepareCoupledInductor(model),
        std::numeric_limits<double>::quiet_NaN(), 0.0)));
}
