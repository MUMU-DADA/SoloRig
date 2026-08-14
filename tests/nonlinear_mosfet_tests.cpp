#include "dsp/nonlinear_mosfet.hpp"
#include "test_framework.hpp"

#include <array>
#include <cmath>
#include <limits>

TEST_CASE("M18-C4 MOSFET evaluator covers cutoff, triode, saturation, reverse and Jacobian") {
    const solo_rig::NonlinearMosfetModel model{.polarity = solo_rig::MosfetPolarity::NChannel,
                                                .threshold_voltage = 1.0,
                                                .transconductance_parameter = 1.0e-3,
                                                .max_current = 1.0,
                                                .max_step = 0.5};
    for (const auto point : std::array<std::array<double, 3>, 5>{
             {{{0.0, 0.0, 0.0}}, {{0.2, 2.0, 0.0}}, {{2.0, 2.0, 0.0}},
              {{0.2, 0.5, 0.0}}, {{-0.3, 2.0, 0.0}}}}) {
        const auto evaluation = solo_rig::EvaluateNonlinearMosfet(model, point[0], point[1], point[2]);
        REQUIRE(evaluation.Succeeded());
        REQUIRE(std::fabs(evaluation.currents[0] + evaluation.currents[1] + evaluation.currents[2]) < 1.0e-15);
        for (std::size_t column = 0; column < 3U; ++column) {
            auto plus = point;
            auto minus = point;
            plus[column] += 1.0e-7;
            minus[column] -= 1.0e-7;
            const auto plus_eval = solo_rig::EvaluateNonlinearMosfet(model, plus[0], plus[1], plus[2]);
            const auto minus_eval = solo_rig::EvaluateNonlinearMosfet(model, minus[0], minus[1], minus[2]);
            REQUIRE(plus_eval.Succeeded());
            REQUIRE(minus_eval.Succeeded());
            for (std::size_t row = 0; row < 3U; ++row) {
                const auto finite_difference = (plus_eval.currents[row] - minus_eval.currents[row]) / 2.0e-7;
                REQUIRE(std::fabs(finite_difference - evaluation.jacobian[row][column]) < 1.0e-7);
            }
        }
    }
}

TEST_CASE("M18-C4 MOSFET N/P mirror and current clamp keep matching derivatives") {
    auto n_channel = solo_rig::NonlinearMosfetModel{.polarity = solo_rig::MosfetPolarity::NChannel,
                                                     .threshold_voltage = 1.0,
                                                     .transconductance_parameter = 1.0e-3,
                                                     .max_current = 1.0e-5,
                                                     .max_step = 0.5};
    auto p_channel = n_channel;
    p_channel.polarity = solo_rig::MosfetPolarity::PChannel;
    const auto n_eval = solo_rig::EvaluateNonlinearMosfet(n_channel, 2.0, 2.0, 0.0);
    const auto p_eval = solo_rig::EvaluateNonlinearMosfet(p_channel, -2.0, -2.0, 0.0);
    REQUIRE(n_eval.Succeeded());
    REQUIRE(p_eval.Succeeded());
    for (std::size_t row = 0; row < 3U; ++row) {
        REQUIRE(std::fabs(n_eval.currents[row] + p_eval.currents[row]) < 1.0e-15);
        for (std::size_t column = 0; column < 3U; ++column) {
            REQUIRE(std::fabs(n_eval.jacobian[row][column] - p_eval.jacobian[row][column]) < 1.0e-12);
        }
    }
    for (std::size_t column = 0; column < 3U; ++column) {
        std::array<double, 3> point{2.0, 2.0, 0.0};
        auto plus = point;
        auto minus = point;
        plus[column] += 1.0e-7;
        minus[column] -= 1.0e-7;
        const auto plus_eval = solo_rig::EvaluateNonlinearMosfet(n_channel, plus[0], plus[1], plus[2]);
        const auto minus_eval = solo_rig::EvaluateNonlinearMosfet(n_channel, minus[0], minus[1], minus[2]);
        const auto finite_difference = (plus_eval.currents[0] - minus_eval.currents[0]) / 2.0e-7;
        REQUIRE(std::fabs(finite_difference - n_eval.jacobian[0][column]) < 1.0e-8);
    }
}

TEST_CASE("M18-C4 MOSFET validates models and clamps extreme finite inputs") {
    solo_rig::NonlinearMosfetModel model{};
    REQUIRE(!solo_rig::ValidateNonlinearMosfetModel(model));
    model.polarity = solo_rig::MosfetPolarity::NChannel;
    model.threshold_voltage = 1.0;
    model.transconductance_parameter = 1.0e-3;
    model.max_current = 0.01;
    model.max_step = 0.5;
    REQUIRE(solo_rig::ValidateNonlinearMosfetModel(model));
    REQUIRE(solo_rig::EvaluateNonlinearMosfet(
                model, std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0).error ==
            solo_rig::NonlinearDeviceError::NonFinite);
    for (const auto point : std::array<std::array<double, 3>, 3>{
             {{{1.0e300, 1.0e300, 0.0}}, {{-1.0e300, 1.0e300, 0.0}},
              {{1.0e300, -1.0e300, 0.0}}}}) {
        const auto result = solo_rig::EvaluateNonlinearMosfet(model, point[0], point[1], point[2]);
        REQUIRE(result.Succeeded());
        for (const auto current : result.currents) {
            REQUIRE(std::isfinite(current));
            REQUIRE(std::fabs(current) <= model.max_current);
        }
    }
    model.threshold_voltage = std::numeric_limits<double>::infinity();
    REQUIRE(!solo_rig::ValidateNonlinearMosfetModel(model));
}
