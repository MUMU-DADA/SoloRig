#include "dsp/nonlinear_jfet_dynamic.hpp"
#include "test_framework.hpp"

#include <array>
#include <cmath>
#include <limits>

namespace {
solo_rig::NonlinearJfetDynamicModel Model(solo_rig::JfetPolarity polarity) {
    return {.static_model = {.polarity = polarity, .pinch_off_voltage = 1.0,
                             .transconductance_parameter = 1.0e-3,
                             .max_current = 0.1, .max_step = 10.0},
            .channel_length_modulation = 0.1, .temperature_celsius = 25.0,
            .gate_saturation_current = 1.0e-12, .gate_thermal_voltage = 0.02585,
            .gate_junction_capacitance = 2.0e-12, .gate_junction_potential = 0.7,
            .gate_grading_coefficient = 0.5, .gate_leakage_temperature_coefficient = 0.01};
}
}

TEST_CASE("M18-C9c JFET dynamic N/P mirror and KCL") {
    const auto n = Model(solo_rig::JfetPolarity::NChannel);
    auto p = n;
    p.static_model.polarity = solo_rig::JfetPolarity::PChannel;
    const auto en = solo_rig::EvaluateNonlinearJfetDynamic(n, 1.5, 0.2, 0.0, 0.1, 0.15, 48000.0);
    const auto ep = solo_rig::EvaluateNonlinearJfetDynamic(p, -1.5, -0.2, 0.0, 0.1, 0.15, 48000.0);
    REQUIRE(en.Succeeded());
    REQUIRE(ep.Succeeded());
    REQUIRE(std::fabs(en.currents[0] + en.currents[1] + en.currents[2]) < 1.0e-12);
    REQUIRE(std::fabs(ep.currents[0] + ep.currents[1] + ep.currents[2]) < 1.0e-12);
    REQUIRE(std::fabs(en.currents[0] + ep.currents[0]) < 1.0e-8);
    REQUIRE(en.gs_capacitance > 0.0);
}

TEST_CASE("M18-C9c JFET dynamic analytic Jacobian matches finite differences") {
    const auto model = Model(solo_rig::JfetPolarity::NChannel);
    const std::array<double, 3> point{1.5, 0.2, 0.0};
    const auto evaluation = solo_rig::EvaluateNonlinearJfetDynamic(model, point[0], point[1], point[2], 0.1, 0.15, 48000.0);
    REQUIRE(evaluation.Succeeded());
    for (std::size_t column = 0; column < 3; ++column) {
        auto plus = point; auto minus = point;
        plus[column] += 1.0e-7; minus[column] -= 1.0e-7;
        const auto ep = solo_rig::EvaluateNonlinearJfetDynamic(model, plus[0], plus[1], plus[2], 0.1, 0.15, 48000.0);
        const auto em = solo_rig::EvaluateNonlinearJfetDynamic(model, minus[0], minus[1], minus[2], 0.1, 0.15, 48000.0);
        REQUIRE(ep.Succeeded()); REQUIRE(em.Succeeded());
        for (std::size_t row = 0; row < 3; ++row) {
            const double fd = (ep.currents[row] - em.currents[row]) / 2.0e-7;
            REQUIRE(std::fabs(fd - evaluation.jacobian[row][column]) < 1.0e-5);
        }
    }
}

TEST_CASE("M18-C9c channel modulation, temperature and invalid boundaries") {
    auto base = Model(solo_rig::JfetPolarity::NChannel);
    auto no_modulation = base; no_modulation.channel_length_modulation = 0.0;
    auto hot = base; hot.temperature_celsius = 150.0;
    const auto a = solo_rig::EvaluateNonlinearJfetDynamic(no_modulation, 2.0, 0.0, 0.0, 0.0, 0.0, 48000.0);
    const auto b = solo_rig::EvaluateNonlinearJfetDynamic(base, 2.0, 0.0, 0.0, 0.0, 0.0, 48000.0);
    const auto c = solo_rig::EvaluateNonlinearJfetDynamic(hot, 2.0, 0.0, 0.0, 0.0, 0.0, 48000.0);
    REQUIRE(a.Succeeded()); REQUIRE(b.Succeeded()); REQUIRE(c.Succeeded());
    REQUIRE(std::fabs(a.currents[0] - b.currents[0]) > 1.0e-8);
    REQUIRE(std::fabs(a.currents[0] - c.currents[0]) > 1.0e-10);
    auto invalid = base; invalid.gate_thermal_voltage = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(!solo_rig::ValidateNonlinearJfetDynamicModel(invalid));
}
