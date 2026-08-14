#include "dsp/nonlinear_jfet.hpp"
#include "test_framework.hpp"

#include <array>
#include <cmath>
#include <limits>

TEST_CASE("M18-C3 JFET evaluator covers cutoff, channel regions, KCL and Jacobian") {
    const solo_rig::NonlinearJfetModel model{.polarity = solo_rig::JfetPolarity::NChannel,
                                              .pinch_off_voltage = 1.0,
                                              .transconductance_parameter = 1.0e-3,
                                              .max_current = 1.0,
                                              .max_step = 0.5};
    for (const auto point : std::array<std::array<double, 3>, 4>{
             {{{0.2, 0.0, 0.0}}, {{2.0, 0.0, 0.0}}, {{0.2, -2.0, 0.0}},
              {{-0.3, 0.0, 0.0}}}}) {
        const auto evaluation = solo_rig::EvaluateNonlinearJfet(model, point[0], point[1], point[2]);
        REQUIRE(evaluation.Succeeded());
        REQUIRE(std::fabs(evaluation.currents[0] + evaluation.currents[1] + evaluation.currents[2]) < 1.0e-15);
        for (std::size_t column = 0; column < 3U; ++column) {
            auto plus = point;
            auto minus = point;
            plus[column] += 1.0e-7;
            minus[column] -= 1.0e-7;
            const auto plus_eval = solo_rig::EvaluateNonlinearJfet(model, plus[0], plus[1], plus[2]);
            const auto minus_eval = solo_rig::EvaluateNonlinearJfet(model, minus[0], minus[1], minus[2]);
            REQUIRE(plus_eval.Succeeded());
            REQUIRE(minus_eval.Succeeded());
            for (std::size_t row = 0; row < 3U; ++row) {
                const auto finite_difference = (plus_eval.currents[row] - minus_eval.currents[row]) / 2.0e-7;
                REQUIRE(std::fabs(finite_difference - evaluation.jacobian[row][column]) < 1.0e-7);
            }
        }
    }
}

TEST_CASE("M18-C3 JFET N/P channel mirror and hard clamp retain Jacobian semantics") {
    auto n_channel = solo_rig::NonlinearJfetModel{.polarity = solo_rig::JfetPolarity::NChannel,
                                                   .pinch_off_voltage = 1.0,
                                                   .transconductance_parameter = 1.0e-3,
                                                   .max_current = 1.0e-5,
                                                   .max_step = 0.5};
    auto p_channel = n_channel;
    p_channel.polarity = solo_rig::JfetPolarity::PChannel;
    const auto n_eval = solo_rig::EvaluateNonlinearJfet(n_channel, 2.0, 0.0, 0.0);
    const auto p_eval = solo_rig::EvaluateNonlinearJfet(p_channel, -2.0, 0.0, 0.0);
    REQUIRE(n_eval.Succeeded());
    REQUIRE(p_eval.Succeeded());
    for (std::size_t index = 0; index < 3U; ++index) {
        REQUIRE(std::fabs(n_eval.currents[index] + p_eval.currents[index]) < 1.0e-15);
        for (std::size_t column = 0; column < 3U; ++column) {
            REQUIRE(std::fabs(n_eval.jacobian[index][column] - p_eval.jacobian[index][column]) < 1.0e-12);
        }
    }
    for (std::size_t column = 0; column < 3U; ++column) {
        std::array<double, 3> point{2.0, 0.0, 0.0};
        auto plus = point;
        auto minus = point;
        plus[column] += 1.0e-7;
        minus[column] -= 1.0e-7;
        const auto plus_eval = solo_rig::EvaluateNonlinearJfet(n_channel, plus[0], plus[1], plus[2]);
        const auto minus_eval = solo_rig::EvaluateNonlinearJfet(n_channel, minus[0], minus[1], minus[2]);
        const auto finite_difference = (plus_eval.currents[0] - minus_eval.currents[0]) / 2.0e-7;
        REQUIRE(std::fabs(finite_difference - n_eval.jacobian[0][column]) < 1.0e-8);
    }
}

TEST_CASE("M18-C3 JFET evaluator rejects invalid and nonfinite models") {
    solo_rig::NonlinearJfetModel model{};
    REQUIRE(!solo_rig::ValidateNonlinearJfetModel(model));
    model.polarity = solo_rig::JfetPolarity::NChannel;
    model.pinch_off_voltage = 1.0;
    model.transconductance_parameter = 1.0e-3;
    model.max_current = 0.01;
    model.max_step = 0.5;
    REQUIRE(solo_rig::ValidateNonlinearJfetModel(model));
    REQUIRE(solo_rig::EvaluateNonlinearJfet(
                model, std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0).error ==
            solo_rig::NonlinearDeviceError::NonFinite);
    model.max_current = std::numeric_limits<double>::infinity();
    REQUIRE(!solo_rig::ValidateNonlinearJfetModel(model));
}

TEST_CASE("M18-C3 JFET extreme finite inputs clamp before square-law overflow") {
    const solo_rig::NonlinearJfetModel model{.polarity = solo_rig::JfetPolarity::NChannel,
                                              .pinch_off_voltage = 1.0,
                                              .transconductance_parameter = 1.0e-3,
                                              .max_current = 0.01,
                                              .max_step = 0.5};
    for (const auto point : std::array<std::array<double, 3>, 4>{
             {{{1.0e300, 1.0e300, 0.0}}, {{-1.0e300, 0.0, 0.0}},
              {{1.0e300, 0.0, 0.0}}, {{0.0, -1.0e300, 0.0}}}}) {
        const auto result = solo_rig::EvaluateNonlinearJfet(model, point[0], point[1], point[2]);
        REQUIRE(result.Succeeded());
        for (const auto current : result.currents) {
            REQUIRE(std::isfinite(current));
            REQUIRE(std::fabs(current) <= model.max_current);
        }
        for (const auto& row : result.jacobian) {
            for (const auto value : row) {
                REQUIRE(std::isfinite(value));
            }
        }
    }
}
