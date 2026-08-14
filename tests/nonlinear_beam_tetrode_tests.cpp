#include "dsp/nonlinear_beam_tetrode.hpp"
#include "test_framework.hpp"

#include <array>
#include <cmath>
#include <limits>

TEST_CASE("M18-C7b beam tetrode evaluator covers capture diversion KCL and full Jacobian") {
    const solo_rig::NonlinearBeamTetrodeModel model{.screen_amplification_factor = 20.0,
                                                    .perveance = 0.001,
                                                    .knee_voltage = 2.0,
                                                    .screen_current_fraction = 0.1,
                                                    .max_current = 1.0,
                                                    .max_step = 0.5};
    const auto knee = solo_rig::EvaluateNonlinearBeamTetrode(model, 2.0, 5.0, 0.0, 0.0);
    REQUIRE(knee.Succeeded());
    REQUIRE(std::fabs(knee.currents[0] - 6.25e-5) < 1.0e-15);
    REQUIRE(std::fabs(knee.currents[1] - 6.25e-6) < 1.0e-15);
    for (const auto point : std::array<std::array<double, 4>, 3>{
             {{{10.0, 5.0, 0.0, 0.0}}, {{2.0, 5.0, 0.2, 0.0}}, {{10.0, 5.0, -0.1, 0.0}}}}) {
        const auto evaluated = solo_rig::EvaluateNonlinearBeamTetrode(
            model, point[0], point[1], point[2], point[3]);
        REQUIRE(evaluated.Succeeded());
        REQUIRE(std::fabs(evaluated.currents[0] + evaluated.currents[1] +
                          evaluated.currents[2] + evaluated.currents[3]) < 1.0e-15);
        for (std::size_t column = 0; column < 4U; ++column) {
            auto plus = point;
            auto minus = point;
            plus[column] += 1.0e-7;
            minus[column] -= 1.0e-7;
            const auto plus_eval = solo_rig::EvaluateNonlinearBeamTetrode(
                model, plus[0], plus[1], plus[2], plus[3]);
            const auto minus_eval = solo_rig::EvaluateNonlinearBeamTetrode(
                model, minus[0], minus[1], minus[2], minus[3]);
            for (std::size_t row = 0; row < 4U; ++row) {
                const double finite_difference =
                    (plus_eval.currents[row] - minus_eval.currents[row]) / 2.0e-7;
                REQUIRE(std::fabs(finite_difference - evaluated.jacobian[row][column]) < 1.0e-7);
            }
        }
    }
    REQUIRE(solo_rig::EvaluateNonlinearBeamTetrode(model, -1.0, 5.0, 1.0, 0.0).currents[0] == 0.0);
    REQUIRE(solo_rig::EvaluateNonlinearBeamTetrode(model, 10.0, -1.0, 1.0, 0.0).currents[0] == 0.0);
    REQUIRE(solo_rig::EvaluateNonlinearBeamTetrode(model, 10.0, 5.0, -0.25, 0.0).currents[0] == 0.0);
}

TEST_CASE("M18-C7b beam tetrode clamp keeps capture derivatives and handles extremes") {
    auto model = solo_rig::NonlinearBeamTetrodeModel{.screen_amplification_factor = 20.0,
                                                     .perveance = 0.001,
                                                     .knee_voltage = 2.0,
                                                     .screen_current_fraction = 0.1,
                                                     .max_current = 1.0e-5,
                                                     .max_step = 0.5};
    const auto clamped = solo_rig::EvaluateNonlinearBeamTetrode(model, 2.0, 5.0, 1.0, 0.0);
    REQUIRE(clamped.Succeeded());
    REQUIRE(std::fabs(clamped.currents[0] - 0.5 * model.max_current) < 1.0e-18);
    REQUIRE(std::fabs(clamped.currents[1] - 0.05 * model.max_current) < 1.0e-18);
    REQUIRE(clamped.jacobian[0][0] > 0.0);
    REQUIRE(clamped.jacobian[1][0] < 0.0);
    REQUIRE(clamped.jacobian[0][1] == 0.0);
    for (const auto point : std::array<std::array<double, 4>, 2>{
             {{{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()}},
              {{-std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                std::numeric_limits<double>::max(), 0.0}}}}) {
        const auto evaluated = solo_rig::EvaluateNonlinearBeamTetrode(
            model, point[0], point[1], point[2], point[3]);
        REQUIRE(evaluated.Succeeded());
        for (const auto current : evaluated.currents) REQUIRE(std::isfinite(current));
        for (const auto& row : evaluated.jacobian) {
            for (const auto derivative : row) REQUIRE(std::isfinite(derivative));
        }
    }
    REQUIRE(solo_rig::EvaluateNonlinearBeamTetrode(
                model, 1.0, 5.0, std::numeric_limits<double>::quiet_NaN(), 0.0).error ==
            solo_rig::NonlinearDeviceError::NonFinite);
    model.screen_current_fraction = 0.6;
    REQUIRE(!solo_rig::ValidateNonlinearBeamTetrodeModel(model));
}
