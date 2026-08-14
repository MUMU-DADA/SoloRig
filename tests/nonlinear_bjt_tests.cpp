#include "dsp/nonlinear_bjt.hpp"
#include "test_framework.hpp"

#include <array>
#include <cmath>
#include <limits>

TEST_CASE("M18-C2 BJT evaluator covers KCL, workspaces and finite-difference Jacobian") {
    const solo_rig::NonlinearBjtModel model{
        .polarity = solo_rig::BjtPolarity::Npn,
        .saturation_current = 1.0e-15,
        .forward_beta = 100.0,
        .reverse_beta = 1.0,
        .thermal_voltage = 0.02585,
        .max_current = 0.01,
        .max_step = 0.5};
    for (const auto point : std::array<std::array<double, 3>, 4>{
             {{{0.0, 0.0, 0.0}}, {{1.0, 0.7, 0.0}}, {{0.0, 0.7, 0.0}},
              {{0.0, 0.7, 1.0}}}}) {
        const auto evaluation = solo_rig::EvaluateNonlinearBjt(model, point[0], point[1], point[2]);
        REQUIRE(evaluation.Succeeded());
        REQUIRE(std::fabs(evaluation.currents[0] + evaluation.currents[1] + evaluation.currents[2]) < 1.0e-15);
        for (std::size_t column = 0; column < 3U; ++column) {
            auto plus = point;
            auto minus = point;
            plus[column] += 1.0e-7;
            minus[column] -= 1.0e-7;
            const auto plus_eval = solo_rig::EvaluateNonlinearBjt(model, plus[0], plus[1], plus[2]);
            const auto minus_eval = solo_rig::EvaluateNonlinearBjt(model, minus[0], minus[1], minus[2]);
            for (std::size_t row = 0; row < 3U; ++row) {
                const auto finite_difference = (plus_eval.currents[row] - minus_eval.currents[row]) / 2.0e-7;
                REQUIRE(std::fabs(finite_difference - evaluation.jacobian[row][column]) < 1.0e-5);
            }
        }
    }
}

TEST_CASE("M18-C2 BJT hard current and exponent limits retain a matching finite Jacobian") {
    const auto verify = [](const solo_rig::NonlinearBjtModel& model,
                           const std::array<double, 3>& point) {
        const auto evaluation = solo_rig::EvaluateNonlinearBjt(model, point[0], point[1], point[2]);
        REQUIRE(evaluation.Succeeded());
        REQUIRE(std::fabs(evaluation.currents[0] + evaluation.currents[1] + evaluation.currents[2]) < 1.0e-15);
        for (std::size_t column = 0; column < 3U; ++column) {
            auto plus = point;
            auto minus = point;
            plus[column] += 1.0e-7;
            minus[column] -= 1.0e-7;
            const auto plus_eval = solo_rig::EvaluateNonlinearBjt(model, plus[0], plus[1], plus[2]);
            const auto minus_eval = solo_rig::EvaluateNonlinearBjt(model, minus[0], minus[1], minus[2]);
            REQUIRE(plus_eval.Succeeded());
            REQUIRE(minus_eval.Succeeded());
            for (std::size_t row = 0; row < 3U; ++row) {
                REQUIRE(std::isfinite(evaluation.jacobian[row][column]));
                const auto finite_difference = (plus_eval.currents[row] - minus_eval.currents[row]) / 2.0e-7;
                REQUIRE(std::fabs(finite_difference - evaluation.jacobian[row][column]) < 1.0e-8);
            }
        }
    };

    const solo_rig::NonlinearBjtModel current_limited{.polarity = solo_rig::BjtPolarity::Npn,
                                                       .saturation_current = 1.0e-15,
                                                       .forward_beta = 100.0,
                                                       .reverse_beta = 1.0,
                                                       .thermal_voltage = 0.02585,
                                                       .max_current = 1.0e-6,
                                                       .max_step = 0.5};
    verify(current_limited, {0.2, 0.8, 0.0});

    auto exponent_limited = current_limited;
    exponent_limited.saturation_current = 1.0e-30;
    exponent_limited.max_current = 1.0e6;
    verify(exponent_limited, {0.0, 100.0, 0.0});
}

TEST_CASE("M18-C2 NPN and PNP Ebers-Moll evaluations mirror by polarity") {
    auto npn = solo_rig::NonlinearBjtModel{.polarity = solo_rig::BjtPolarity::Npn,
                                           .saturation_current = 1.0e-15,
                                           .forward_beta = 100.0,
                                           .reverse_beta = 1.0,
                                           .thermal_voltage = 0.02585,
                                           .max_current = 0.01,
                                           .max_step = 0.5};
    auto pnp = npn;
    pnp.polarity = solo_rig::BjtPolarity::Pnp;
    const auto npn_eval = solo_rig::EvaluateNonlinearBjt(npn, 0.2, 0.7, 0.0);
    const auto pnp_eval = solo_rig::EvaluateNonlinearBjt(pnp, -0.2, -0.7, 0.0);
    REQUIRE(npn_eval.Succeeded());
    REQUIRE(pnp_eval.Succeeded());
    for (std::size_t index = 0; index < 3U; ++index) {
        REQUIRE(std::fabs(npn_eval.currents[index] + pnp_eval.currents[index]) < 1.0e-12);
        for (std::size_t column = 0; column < 3U; ++column) {
            REQUIRE(std::fabs(npn_eval.jacobian[index][column] - pnp_eval.jacobian[index][column]) < 1.0e-12);
        }
    }
    const std::array<double, 3> pnp_point{-0.2, -0.7, 0.0};
    for (std::size_t column = 0; column < 3U; ++column) {
        auto plus = pnp_point;
        auto minus = pnp_point;
        plus[column] += 1.0e-7;
        minus[column] -= 1.0e-7;
        const auto plus_eval = solo_rig::EvaluateNonlinearBjt(pnp, plus[0], plus[1], plus[2]);
        const auto minus_eval = solo_rig::EvaluateNonlinearBjt(pnp, minus[0], minus[1], minus[2]);
        for (std::size_t row = 0; row < 3U; ++row) {
            const auto finite_difference = (plus_eval.currents[row] - minus_eval.currents[row]) / 2.0e-7;
            REQUIRE(std::fabs(finite_difference - pnp_eval.jacobian[row][column]) < 1.0e-5);
        }
    }
}

TEST_CASE("M18-C2 BJT evaluator rejects invalid and nonfinite models") {
    solo_rig::NonlinearBjtModel model{};
    REQUIRE(!solo_rig::ValidateNonlinearBjtModel(model));
    model.polarity = solo_rig::BjtPolarity::Npn;
    model.saturation_current = 1.0e-15;
    model.forward_beta = 100.0;
    model.reverse_beta = 1.0;
    model.thermal_voltage = 0.02585;
    model.max_current = 0.01;
    model.max_step = 0.5;
    REQUIRE(solo_rig::ValidateNonlinearBjtModel(model));
    REQUIRE(solo_rig::EvaluateNonlinearBjt(
                model, std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0).error ==
            solo_rig::NonlinearDeviceError::NonFinite);
}
