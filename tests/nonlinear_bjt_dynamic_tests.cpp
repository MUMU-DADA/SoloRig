#include "dsp/nonlinear_bjt_dynamic.hpp"
#include "test_framework.hpp"

#include <array>
#include <cmath>
#include <limits>

namespace {
solo_rig::NonlinearBjtDynamicModel Model(solo_rig::BjtPolarity polarity) {
    return {.static_model = {.polarity = polarity, .saturation_current = 1.0e-15,
                             .forward_beta = 100.0, .reverse_beta = 1.0,
                             .thermal_voltage = 0.02585, .max_current = 0.01, .max_step = 0.5},
            .early_voltage = 40.0, .be_junction_capacitance = 2.0e-12,
            .bc_junction_capacitance = 1.0e-12, .be_junction_potential = 0.7,
            .bc_junction_potential = 0.6, .be_grading_coefficient = 0.5,
            .bc_grading_coefficient = 0.4, .temperature_celsius = 25.0,
            .leakage_temperature_coefficient = 0.0};
}
}

TEST_CASE("M18-C9b dynamic BJT preserves KCL, mirrors NPN/PNP and exposes capacitance") {
    const auto npn = Model(solo_rig::BjtPolarity::Npn);
    auto pnp = npn;
    pnp.static_model.polarity = solo_rig::BjtPolarity::Pnp;
    const auto n = solo_rig::EvaluateNonlinearBjtDynamic(npn, 1.0, 0.72, 0.0, 0.68, 0.0, 48000.0);
    const auto p = solo_rig::EvaluateNonlinearBjtDynamic(pnp, -1.0, -0.72, 0.0, 0.68, 0.0, 48000.0);
    REQUIRE(n.Succeeded());
    REQUIRE(p.Succeeded());
    REQUIRE(std::fabs(n.currents[0] + n.currents[1] + n.currents[2]) < 1.0e-12);
    REQUIRE(std::fabs(p.currents[0] + p.currents[1] + p.currents[2]) < 1.0e-12);
    REQUIRE(n.be_capacitance > npn.be_junction_capacitance);
    REQUIRE(n.bc_capacitance > 0.0);
    REQUIRE(std::fabs(n.currents[0] + p.currents[0]) < 1.0e-9);
}

TEST_CASE("M18-C9b dynamic BJT analytic 3x3 Jacobian matches finite differences") {
    auto model = Model(solo_rig::BjtPolarity::Npn);
    const std::array<double, 3> point{1.0, 0.68, 0.0};
    const auto evaluation = solo_rig::EvaluateNonlinearBjtDynamic(
        model, point[0], point[1], point[2], 0.65, 0.1, 48000.0);
    REQUIRE(evaluation.Succeeded());
    for (std::size_t column = 0; column < 3; ++column) {
        auto plus = point;
        auto minus = point;
        plus[column] += 1.0e-7;
        minus[column] -= 1.0e-7;
        const auto ep = solo_rig::EvaluateNonlinearBjtDynamic(model, plus[0], plus[1], plus[2], 0.65, 0.1, 48000.0);
        const auto em = solo_rig::EvaluateNonlinearBjtDynamic(model, minus[0], minus[1], minus[2], 0.65, 0.1, 48000.0);
        REQUIRE(ep.Succeeded());
        REQUIRE(em.Succeeded());
        for (std::size_t row = 0; row < 3; ++row) {
            const double fd = (ep.currents[row] - em.currents[row]) / 2.0e-7;
            REQUIRE(std::fabs(fd - evaluation.jacobian[row][column]) < 1.0e-5);
        }
    }
}

TEST_CASE("M18-C9b Early effect, temperature and invalid boundaries are explicit") {
    auto cold = Model(solo_rig::BjtPolarity::Npn);
    auto hot = cold;
    hot.temperature_celsius = 150.0;
    auto disabled = cold;
    disabled.early_voltage = 0.0;
    const auto a = solo_rig::EvaluateNonlinearBjtDynamic(disabled, 2.0, 0.65, 0.0, 0.0, 0.0, 48000.0);
    const auto b = solo_rig::EvaluateNonlinearBjtDynamic(cold, 2.0, 0.65, 0.0, 0.0, 0.0, 48000.0);
    const auto t = solo_rig::EvaluateNonlinearBjtDynamic(hot, 2.0, 0.65, 0.0, 0.0, 0.0, 48000.0);
    REQUIRE(a.Succeeded());
    REQUIRE(b.Succeeded());
    REQUIRE(t.Succeeded());
    REQUIRE(std::fabs(a.currents[0] - b.currents[0]) > 1.0e-12);
    REQUIRE(std::fabs(t.currents[0] - b.currents[0]) > 1.0e-15);
    auto invalid = cold;
    invalid.temperature_celsius = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(!solo_rig::ValidateNonlinearBjtDynamicModel(invalid));
    REQUIRE(solo_rig::EvaluateNonlinearBjtDynamic(cold, std::numeric_limits<double>::infinity(), 0.0, 0.0,
                                                   0.0, 0.0, 48000.0).error == solo_rig::NonlinearDeviceError::NonFinite);
}
