#include "dsp/nonlinear_triode.hpp"
#include "test_framework.hpp"

#include <array>
#include <cmath>
#include <limits>

TEST_CASE("M18-C6 triode evaluator covers cutoff space-charge KCL and full Jacobian") {
    const solo_rig::NonlinearTriodeModel model{.amplification_factor = 100.0,
                                               .perveance = 0.001,
                                               .max_current = 1.0,
                                               .max_step = 0.5};
    for (const auto point : std::array<std::array<double, 3>, 3>{
             {{{10.0, 0.0, 0.0}}, {{10.0, 0.2, 0.0}}, {{10.0, -1.0, 0.0}}}}) {
        const auto evaluated = solo_rig::EvaluateNonlinearTriode(model, point[0], point[1], point[2]);
        REQUIRE(evaluated.Succeeded());
        REQUIRE(std::fabs(evaluated.currents[0] + evaluated.currents[1] + evaluated.currents[2]) < 1.0e-15);
        for (std::size_t column = 0; column < 3U; ++column) {
            auto plus = point;
            auto minus = point;
            plus[column] += 1.0e-7;
            minus[column] -= 1.0e-7;
            const auto plus_eval = solo_rig::EvaluateNonlinearTriode(model, plus[0], plus[1], plus[2]);
            const auto minus_eval = solo_rig::EvaluateNonlinearTriode(model, minus[0], minus[1], minus[2]);
            for (std::size_t row = 0; row < 3U; ++row) {
                const auto finite_difference = (plus_eval.currents[row] - minus_eval.currents[row]) / 2.0e-7;
                REQUIRE(std::fabs(finite_difference - evaluated.jacobian[row][column]) < 1.0e-7);
            }
        }
    }
    const auto reverse = solo_rig::EvaluateNonlinearTriode(model, -10.0, 2.0, 0.0);
    REQUIRE(reverse.Succeeded());
    REQUIRE(reverse.currents[0] == 0.0);
}

TEST_CASE("M18-C6 triode validation and pre-power clamp handle extreme inputs") {
    auto model = solo_rig::NonlinearTriodeModel{.amplification_factor = 100.0,
                                                .perveance = 0.001,
                                                .max_current = 1.0e-5,
                                                .max_step = 0.5};
    REQUIRE(solo_rig::ValidateNonlinearTriodeModel(model));
    const auto clamped = solo_rig::EvaluateNonlinearTriode(model, 10.0, 0.0, 0.0);
    REQUIRE(clamped.Succeeded());
    REQUIRE(clamped.currents[0] == model.max_current);
    for (const auto& row : clamped.jacobian) {
        for (const auto value : row) {
            REQUIRE(value == 0.0);
        }
    }
    for (const auto point : std::array<std::array<double, 3>, 3>{
             {{{1.0e300, 1.0e300, -1.0e300}}, {{-1.0e300, 1.0e300, 1.0e300}},
              {{1.0e300, -1.0e300, 0.0}}}}) {
        const auto evaluated = solo_rig::EvaluateNonlinearTriode(model, point[0], point[1], point[2]);
        REQUIRE(evaluated.Succeeded());
        for (const auto current : evaluated.currents) {
            REQUIRE(std::isfinite(current));
        }
    }
    REQUIRE(solo_rig::EvaluateNonlinearTriode(
                model, 0.0, std::numeric_limits<double>::quiet_NaN(), 0.0).error ==
            solo_rig::NonlinearDeviceError::NonFinite);
    model.perveance = std::numeric_limits<double>::infinity();
    REQUIRE(!solo_rig::ValidateNonlinearTriodeModel(model));
}
