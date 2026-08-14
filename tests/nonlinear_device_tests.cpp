#include "dsp/nonlinear_device.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <array>
#include <limits>

TEST_CASE("M18-C1 diode IV and analytic Jacobian match the clean-room formula") {
    const solo_rig::NonlinearDeviceModel model{
        .kind = solo_rig::NonlinearDeviceKind::Diode,
        .saturation_current = 1.0e-12,
        .thermal_voltage = 0.02585,
        .max_step = 0.5};
    const auto zero = solo_rig::EvaluateNonlinearDevice(model, 0.0);
    REQUIRE(zero.Succeeded());
    REQUIRE(std::fabs(zero.current) < 1.0e-24);
    REQUIRE(std::fabs(zero.jacobian - model.saturation_current / model.thermal_voltage) < 1.0e-20);

    constexpr double voltage = 0.5;
    const auto point = solo_rig::EvaluateNonlinearDevice(model, voltage);
    const auto exponential = std::exp(voltage / model.thermal_voltage);
    REQUIRE(point.Succeeded());
    REQUIRE(std::fabs(point.current - model.saturation_current * (exponential - 1.0)) < 1.0e-12);
    REQUIRE(std::fabs(point.jacobian - model.saturation_current / model.thermal_voltage * exponential) < 1.0e-12);
}

TEST_CASE("M18-C1 Zener IV covers forward zero-bias and reverse breakdown") {
    const solo_rig::NonlinearDeviceModel model{
        .kind = solo_rig::NonlinearDeviceKind::Zener,
        .saturation_current = 1.0e-12,
        .thermal_voltage = 0.02585,
        .breakdown_voltage = 5.1,
        .breakdown_current = 1.0e-6,
        .breakdown_slope = 0.1,
        .max_step = 1.0};
    const auto zero = solo_rig::EvaluateNonlinearDevice(model, 0.0);
    const auto forward = solo_rig::EvaluateNonlinearDevice(model, 0.5);
    const auto reverse = solo_rig::EvaluateNonlinearDevice(model, -5.2);
    REQUIRE(zero.Succeeded());
    REQUIRE(std::fabs(zero.current) < 1.0e-24);
    REQUIRE(forward.Succeeded());
    REQUIRE(forward.current > 0.0);
    REQUIRE(reverse.Succeeded());
    REQUIRE(reverse.current < -1.0e-6);
    REQUIRE(reverse.jacobian > 0.0);
}

TEST_CASE("M18-C1 nonlinear models reject nonfinite and unsafe parameters") {
    auto model = solo_rig::NonlinearDeviceModel{};
    REQUIRE(!solo_rig::ValidateNonlinearDeviceModel(model));
    model.kind = solo_rig::NonlinearDeviceKind::Diode;
    model.saturation_current = 1.0e-12;
    model.thermal_voltage = 0.02585;
    model.max_step = 0.5;
    REQUIRE(solo_rig::ValidateNonlinearDeviceModel(model));
    REQUIRE(solo_rig::EvaluateNonlinearDevice(model, std::numeric_limits<double>::quiet_NaN()).error ==
            solo_rig::NonlinearDeviceError::NonFinite);
    model.thermal_voltage = std::numeric_limits<double>::infinity();
    REQUIRE(!solo_rig::ValidateNonlinearDeviceModel(model));
}

TEST_CASE("M18-C1 nonlinear evaluation keeps extreme finite inputs bounded") {
    const solo_rig::NonlinearDeviceModel model{
        .kind = solo_rig::NonlinearDeviceKind::Zener,
        .saturation_current = 1.0e-12,
        .thermal_voltage = 0.02585,
        .breakdown_voltage = 5.1,
        .breakdown_current = 1.0e-6,
        .breakdown_slope = 0.1,
        .max_step = 10.0};
    for (const double voltage : {-1.0e12, 1.0e12}) {
        const auto point = solo_rig::EvaluateNonlinearDevice(model, voltage);
        REQUIRE(point.Succeeded());
        REQUIRE(std::isfinite(point.current));
        REQUIRE(std::isfinite(point.jacobian));
    }
}

TEST_CASE("M18-C2 BJT evaluator covers KCL, workspaces and finite-difference Jacobian") {
    const solo_rig::NonlinearBjtModel model{
        .polarity = solo_rig::BjtPolarity::Npn,
        .saturation_current = 1.0e-15,
        .forward_beta = 100.0,
        .reverse_beta = 1.0,
        .thermal_voltage = 0.02585,
        .max_current = 0.01,
        .max_step = 0.5};
    // zero/cutoff, forward-active, saturation and reverse-active.
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
    REQUIRE(solo_rig::EvaluateNonlinearBjt(model, std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0).error ==
            solo_rig::NonlinearDeviceError::NonFinite);
}

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
    REQUIRE(solo_rig::EvaluateNonlinearJfet(model, std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0).error ==
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

TEST_CASE("M18-C5 bounded amplifier evaluator matches transfer Jacobian and clamp semantics") {
    const solo_rig::NonlinearAmplifierModel opamp{.kind = solo_rig::AmplifierKind::OpAmp,
                                                   .gain = 100.0,
                                                   .minimum_output = -2.0,
                                                   .maximum_output = 2.0,
                                                   .max_step = 0.5};
    for (const auto point : std::array<std::array<double, 2>, 3>{
             {{{0.01, 0.0}}, {{0.0, 0.01}}, {{0.25, 0.25}}}}) {
        const auto evaluated = solo_rig::EvaluateNonlinearAmplifier(opamp, point[0], point[1]);
        REQUIRE(evaluated.Succeeded());
        for (std::size_t column = 0; column < 2U; ++column) {
            auto plus = point;
            auto minus = point;
            plus[column] += 1.0e-7;
            minus[column] -= 1.0e-7;
            const auto plus_value = solo_rig::EvaluateNonlinearAmplifier(opamp, plus[0], plus[1]);
            const auto minus_value = solo_rig::EvaluateNonlinearAmplifier(opamp, minus[0], minus[1]);
            REQUIRE(std::fabs((plus_value.output - minus_value.output) / 2.0e-7 -
                              evaluated.jacobian[column]) < 1.0e-7);
        }
    }
    for (const double differential : {-1.0, 1.0}) {
        const auto clamped = solo_rig::EvaluateNonlinearAmplifier(opamp, differential, 0.0);
        REQUIRE(clamped.Succeeded());
        REQUIRE(clamped.output == (differential < 0.0 ? -2.0 : 2.0));
        REQUIRE(clamped.jacobian[0] == 0.0);
        REQUIRE(clamped.jacobian[1] == 0.0);
    }
}

TEST_CASE("M18-C5 amplifier models reject invalid data and clamp extreme finite input before overflow") {
    auto ota = solo_rig::NonlinearAmplifierModel{.kind = solo_rig::AmplifierKind::Ota,
                                                  .gain = 1.0e6,
                                                  .minimum_output = -0.002,
                                                  .maximum_output = 0.002,
                                                  .max_step = 0.5};
    REQUIRE(solo_rig::ValidateNonlinearAmplifierModel(ota));
    for (const double input : {-1.0e300, 1.0e300}) {
        const auto evaluated = solo_rig::EvaluateNonlinearAmplifier(ota, input, 0.0);
        REQUIRE(evaluated.Succeeded());
        REQUIRE(std::isfinite(evaluated.output));
        REQUIRE(std::fabs(evaluated.output) == 0.002);
        REQUIRE(evaluated.jacobian[0] == 0.0);
    }
    REQUIRE(solo_rig::EvaluateNonlinearAmplifier(
                ota, std::numeric_limits<double>::quiet_NaN(), 0.0).error ==
            solo_rig::NonlinearDeviceError::NonFinite);
    ota.maximum_output = ota.minimum_output;
    REQUIRE(!solo_rig::ValidateNonlinearAmplifierModel(ota));
    ota.maximum_output = 0.002;
    ota.gain = std::numeric_limits<double>::infinity();
    REQUIRE(!solo_rig::ValidateNonlinearAmplifierModel(ota));
}
