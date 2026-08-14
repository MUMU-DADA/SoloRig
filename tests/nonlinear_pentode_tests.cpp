#include "dsp/nonlinear_pentode.hpp"
#include "test_framework.hpp"

#include <array>
#include <cmath>
#include <limits>

TEST_CASE("M18-C7a pentode evaluator covers cutoff split KCL and full Jacobian") {
    const solo_rig::NonlinearPentodeModel model{.screen_amplification_factor = 20.0,
                                                .perveance = 0.001,
                                                .knee_voltage = 2.0,
                                                .screen_current_fraction = 0.1,
                                                .max_current = 1.0,
                                                .max_step = 0.5};
    for (const auto point : std::array<std::array<double, 4>, 3>{
             {{{10.0, 5.0, 0.0, 0.0}}, {{1.0, 5.0, 0.2, 0.0}}, {{10.0, 5.0, -1.0, 0.0}}}}) {
        const auto evaluated = solo_rig::EvaluateNonlinearPentode(
            model, point[0], point[1], point[2], point[3]);
        REQUIRE(evaluated.Succeeded());
        REQUIRE(std::fabs(evaluated.currents[0] + evaluated.currents[1] +
                          evaluated.currents[2] + evaluated.currents[3]) < 1.0e-15);
        for (std::size_t column = 0; column < 4U; ++column) {
            auto plus = point;
            auto minus = point;
            plus[column] += 1.0e-7;
            minus[column] -= 1.0e-7;
            const auto plus_eval = solo_rig::EvaluateNonlinearPentode(
                model, plus[0], plus[1], plus[2], plus[3]);
            const auto minus_eval = solo_rig::EvaluateNonlinearPentode(
                model, minus[0], minus[1], minus[2], minus[3]);
            for (std::size_t row = 0; row < 4U; ++row) {
                const double finite_difference =
                    (plus_eval.currents[row] - minus_eval.currents[row]) / 2.0e-7;
                REQUIRE(std::fabs(finite_difference - evaluated.jacobian[row][column]) < 1.0e-7);
            }
        }
    }
    REQUIRE(solo_rig::EvaluateNonlinearPentode(model, -1.0, 5.0, 1.0, 0.0).currents[0] == 0.0);
    REQUIRE(solo_rig::EvaluateNonlinearPentode(model, 10.0, -1.0, 1.0, 0.0).currents[0] == 0.0);
}

TEST_CASE("M18-C7a pentode clamp remains differentiable in plate knee and handles extremes") {
    auto model = solo_rig::NonlinearPentodeModel{.screen_amplification_factor = 20.0,
                                                 .perveance = 0.001,
                                                 .knee_voltage = 2.0,
                                                 .screen_current_fraction = 0.1,
                                                 .max_current = 1.0e-5,
                                                 .max_step = 0.5};
    const auto clamped = solo_rig::EvaluateNonlinearPentode(model, 1.0, 5.0, 1.0, 0.0);
    REQUIRE(clamped.Succeeded());
    REQUIRE(clamped.currents[1] == model.screen_current_fraction * model.max_current);
    REQUIRE(clamped.jacobian[0][0] > 0.0);
    REQUIRE(clamped.jacobian[0][1] == 0.0);
    for (const auto point : std::array<std::array<double, 4>, 2>{
             {{{1.0e300, 1.0e300, 1.0e300, -1.0e300}},
              {{-1.0e300, 1.0e300, 1.0e300, 0.0}}}}) {
        const auto evaluated = solo_rig::EvaluateNonlinearPentode(
            model, point[0], point[1], point[2], point[3]);
        REQUIRE(evaluated.Succeeded());
        for (const auto current : evaluated.currents) REQUIRE(std::isfinite(current));
    }
    REQUIRE(solo_rig::EvaluateNonlinearPentode(
                model, 1.0, 5.0, std::numeric_limits<double>::quiet_NaN(), 0.0).error ==
            solo_rig::NonlinearDeviceError::NonFinite);
    model.knee_voltage = std::numeric_limits<double>::infinity();
    REQUIRE(!solo_rig::ValidateNonlinearPentodeModel(model));
}
