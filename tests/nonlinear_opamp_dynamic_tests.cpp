#include "dsp/nonlinear_opamp_dynamic.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>

namespace {
solo_rig::NonlinearOpAmpDynamicModel Model() {
    return {.gain = 100.0, .gain_bandwidth_hz = 1000.0, .slew_rate = 1.0e6,
            .input_resistance = 1.0e6, .input_bias_current = 2.0e-6, .input_offset = 0.01,
            .output_resistance = 100.0, .output_current_limit = 0.1,
            .supply_minimum = -12.0, .supply_maximum = 12.0,
            .minimum_output = -10.0, .maximum_output = 10.0, .max_step = 1.0};
}
}

TEST_CASE("M18-C9e dynamic opamp evaluator has four-terminal KCL and finite-difference Jacobian") {
    const auto model = Model();
    const double point[6] = {0.2, -0.1, 0.03, -0.01, 0.0, 1.0 / 48000.0};
    const auto evaluated = solo_rig::EvaluateNonlinearOpAmpDynamic(
        model, point[0], point[1], point[2], point[3], point[4], point[5]);
    REQUIRE(evaluated.Succeeded());
    REQUIRE(std::fabs(evaluated.currents[0] + evaluated.currents[1] + evaluated.currents[2] + evaluated.currents[3]) < 1.0e-15);
    for (std::size_t column = 0; column < 4U; ++column) {
        double plus[6] = {point[0], point[1], point[2], point[3], point[4], point[5]};
        double minus[6] = {point[0], point[1], point[2], point[3], point[4], point[5]};
        plus[column] += 1.0e-7;
        minus[column] -= 1.0e-7;
        const auto p = solo_rig::EvaluateNonlinearOpAmpDynamic(model, plus[0], plus[1], plus[2], plus[3], plus[4], plus[5]);
        const auto m = solo_rig::EvaluateNonlinearOpAmpDynamic(model, minus[0], minus[1], minus[2], minus[3], minus[4], minus[5]);
        for (std::size_t row = 0; row < 4U; ++row) {
            REQUIRE(std::fabs((p.currents[row] - m.currents[row]) / 2.0e-7 - evaluated.jacobian[row][column]) < 1.0e-8);
        }
    }
}

TEST_CASE("M18-C9e pole, slew, rails, current limit, bias and invalid boundaries are explicit") {
    auto model = Model();
    const double dt = 1.0 / 48000.0;
    const auto slow = solo_rig::EvaluateNonlinearOpAmpDynamic(model, 0.0, 0.0, 1.0, 0.0, 0.0, dt);
    REQUIRE(slow.state_voltage > 0.0);
    model.slew_rate = 1.0;
    const auto slew = solo_rig::EvaluateNonlinearOpAmpDynamic(model, 0.0, 0.0, 1.0, 0.0, 0.0, dt);
    REQUIRE(std::fabs(slew.state_voltage - dt) < 1.0e-15);
    model.supply_maximum = 0.5;
    model.maximum_output = 0.5;
    const auto rail = solo_rig::EvaluateNonlinearOpAmpDynamic(model, 0.0, 0.0, 10.0, 0.0, 0.0, dt);
    REQUIRE(rail.state_voltage <= 0.5 + 1.0e-12);
    model.output_current_limit = 1.0e-3;
    const auto limited = solo_rig::EvaluateNonlinearOpAmpDynamic(model, -1.0, 1.0, 0.0, 0.0, 1.0, dt);
    REQUIRE(std::fabs(limited.currents[0]) <= 1.0e-3 + 1.0e-15);
    REQUIRE(solo_rig::EvaluateNonlinearOpAmpDynamic(model, std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0, 0.0, 0.0, dt).error == solo_rig::NonlinearDeviceError::NonFinite);
    const auto extreme = solo_rig::EvaluateNonlinearOpAmpDynamic(
        model, 1.0e308, -1.0e308, 1.0e308, -1.0e308, 0.0, dt);
    REQUIRE(extreme.Succeeded());
    for (const double current : extreme.currents) REQUIRE(std::isfinite(current));
    model.output_resistance = 0.0;
    REQUIRE(!solo_rig::ValidateNonlinearOpAmpDynamicModel(model));
}
