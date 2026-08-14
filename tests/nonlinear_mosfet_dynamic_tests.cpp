#include "dsp/nonlinear_mosfet_dynamic.hpp"
#include "test_framework.hpp"

#include <array>
#include <cmath>
#include <limits>

namespace {
solo_rig::NonlinearMosfetDynamicModel Model(solo_rig::MosfetPolarity polarity) {
    return {.static_model = {.polarity = polarity, .threshold_voltage = 1.0,
                             .transconductance_parameter = 1.0e-3,
                             .max_current = 0.2, .max_step = 10.0},
            .channel_length_modulation = 0.1, .subthreshold_current = 1.0e-10,
            .subthreshold_slope_voltage = 0.08, .temperature_celsius = 25.0,
            .body_diode_saturation_current = 1.0e-12,
            .body_diode_thermal_voltage = 0.02585,
            .gate_junction_capacitance = 2.0e-12,
            .gate_junction_potential = 0.7, .gate_grading_coefficient = 0.5};
}
}

TEST_CASE("M18-C9d MOSFET dynamic N/P mirror and KCL") {
    const auto n = Model(solo_rig::MosfetPolarity::NChannel);
    auto p = n;
    p.static_model.polarity = solo_rig::MosfetPolarity::PChannel;
    const auto en = solo_rig::EvaluateNonlinearMosfetDynamic(n, 1.5, 2.0, 0.0, 0.1, 0.15, 48000.0);
    const auto ep = solo_rig::EvaluateNonlinearMosfetDynamic(p, -1.5, -2.0, 0.0, 0.1, 0.15, 48000.0);
    REQUIRE(en.Succeeded());
    REQUIRE(ep.Succeeded());
    REQUIRE(std::fabs(en.currents[0] + en.currents[1] + en.currents[2]) < 1.0e-12);
    REQUIRE(std::fabs(ep.currents[0] + ep.currents[1] + ep.currents[2]) < 1.0e-12);
    for (std::size_t row = 0; row < 3; ++row) {
        REQUIRE(std::fabs(en.currents[row] + ep.currents[row]) < 1.0e-8);
        for (std::size_t column = 0; column < 3; ++column) {
            REQUIRE(std::fabs(en.jacobian[row][column] - ep.jacobian[row][column]) < 1.0e-7);
        }
    }
}

TEST_CASE("M18-C9d channel remains drain/source symmetric while body diode is directional") {
    auto channel_only = Model(solo_rig::MosfetPolarity::NChannel);
    channel_only.body_diode_saturation_current = 0.0;
    const auto forward = solo_rig::EvaluateNonlinearMosfetDynamic(
        channel_only, 1.5, 1.8, 0.0, 0.0, 0.0, 48000.0);
    const auto swapped = solo_rig::EvaluateNonlinearMosfetDynamic(
        channel_only, 0.0, 1.8, 1.5, 0.0, 0.0, 48000.0);
    REQUIRE(forward.Succeeded()); REQUIRE(swapped.Succeeded());
    REQUIRE(std::fabs(forward.currents[0] - swapped.currents[2]) < 1.0e-8);
    REQUIRE(std::fabs(forward.currents[2] - swapped.currents[0]) < 1.0e-8);
    const auto with_body = Model(solo_rig::MosfetPolarity::NChannel);
    const auto diode_forward = solo_rig::EvaluateNonlinearMosfetDynamic(
        with_body, -0.6, 0.0, 0.0, 0.0, 0.0, 48000.0);
    const auto diode_swapped = solo_rig::EvaluateNonlinearMosfetDynamic(
        with_body, 0.0, 0.0, -0.6, 0.0, 0.0, 48000.0);
    REQUIRE(diode_forward.Succeeded()); REQUIRE(diode_swapped.Succeeded());
    REQUIRE(std::fabs(diode_forward.currents[0] - diode_swapped.currents[2]) > 1.0e-6);
}

TEST_CASE("M18-C9d MOSFET dynamic analytic Jacobian matches finite differences") {
    const auto model = Model(solo_rig::MosfetPolarity::NChannel);
    // Reverse vds forward-biases the source-tied body diode without reaching
    // its current clamp, while the gate remains below threshold.
    const std::array<double, 3> point{-0.6, 0.8, 0.0};
    const auto evaluation = solo_rig::EvaluateNonlinearMosfetDynamic(
        model, point[0], point[1], point[2], 0.1, 0.15, 48000.0);
    auto no_body_model = model; no_body_model.body_diode_saturation_current = 0.0;
    const auto no_body_eval = solo_rig::EvaluateNonlinearMosfetDynamic(
        no_body_model, point[0], point[1], point[2], 0.1, 0.15, 48000.0);
    REQUIRE(evaluation.Succeeded());
    for (std::size_t column = 0; column < 3; ++column) {
        auto plus = point; auto minus = point;
        plus[column] += 1.0e-7; minus[column] -= 1.0e-7;
        const auto ep = solo_rig::EvaluateNonlinearMosfetDynamic(
            model, plus[0], plus[1], plus[2], 0.1, 0.15, 48000.0);
        const auto em = solo_rig::EvaluateNonlinearMosfetDynamic(
            model, minus[0], minus[1], minus[2], 0.1, 0.15, 48000.0);
        REQUIRE(ep.Succeeded()); REQUIRE(em.Succeeded());
        for (std::size_t row = 0; row < 3; ++row) {
            const double finite_difference = (ep.currents[row] - em.currents[row]) / 2.0e-7;
            REQUIRE(std::fabs(finite_difference - evaluation.jacobian[row][column]) < 1.0e-4);
        }
    }
}

TEST_CASE("M18-C9d MOSFET dynamic covers CLM subthreshold temperature and body diode") {
    auto base = Model(solo_rig::MosfetPolarity::NChannel);
    auto no_clm = base; no_clm.channel_length_modulation = 0.0;
    auto no_subthreshold = base; no_subthreshold.subthreshold_current = 0.0;
    auto hot = base; hot.temperature_celsius = 150.0;
    const auto clm_a = solo_rig::EvaluateNonlinearMosfetDynamic(no_clm, 2.0, 2.0, 0.0, 0.0, 0.0, 48000.0);
    const auto clm_b = solo_rig::EvaluateNonlinearMosfetDynamic(base, 2.0, 2.0, 0.0, 0.0, 0.0, 48000.0);
    REQUIRE(clm_a.Succeeded()); REQUIRE(clm_b.Succeeded());
    REQUIRE(std::fabs(clm_a.currents[0] - clm_b.currents[0]) > 1.0e-10);
    const auto sub = solo_rig::EvaluateNonlinearMosfetDynamic(base, 0.2, 0.8, 0.0, 0.0, 0.0, 48000.0);
    const auto no_sub = solo_rig::EvaluateNonlinearMosfetDynamic(no_subthreshold, 0.2, 0.8, 0.0, 0.0, 0.0, 48000.0);
    REQUIRE(sub.Succeeded()); REQUIRE(no_sub.Succeeded());
    REQUIRE(std::fabs(sub.currents[0] - no_sub.currents[0]) > 1.0e-12);
    const auto warm = solo_rig::EvaluateNonlinearMosfetDynamic(hot, 2.0, 1.6, 0.0, 0.0, 0.0, 48000.0);
    REQUIRE(warm.Succeeded());
    REQUIRE(std::fabs(warm.currents[0] - clm_b.currents[0]) > 1.0e-10);
    const auto body = solo_rig::EvaluateNonlinearMosfetDynamic(base, -0.8, 0.0, 0.0, 0.0, 0.0, 48000.0);
    REQUIRE(body.Succeeded());
    REQUIRE(body.currents[0] < 0.0);
    REQUIRE(body.currents[2] > 0.0);
    REQUIRE(body.gs_capacitance > 0.0);
    REQUIRE(body.gd_capacitance > 0.0);
}

TEST_CASE("M18-C9d MOSFET dynamic rejects unsafe and nonfinite boundaries") {
    auto model = Model(solo_rig::MosfetPolarity::NChannel);
    REQUIRE(solo_rig::ValidateNonlinearMosfetDynamicModel(model));
    model.subthreshold_slope_voltage = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(!solo_rig::ValidateNonlinearMosfetDynamicModel(model));
    model = Model(solo_rig::MosfetPolarity::NChannel);
    REQUIRE(solo_rig::EvaluateNonlinearMosfetDynamic(
                model, std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0, 0.0, 0.0, 48000.0).error ==
            solo_rig::NonlinearDeviceError::NonFinite);
    REQUIRE(solo_rig::EvaluateNonlinearMosfetDynamic(model, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0).error ==
            solo_rig::NonlinearDeviceError::NonFinite);
}
