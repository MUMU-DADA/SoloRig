#include "dsp/dynamic_power_supply.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>

namespace {
solo_rig::DynamicPowerSupplyModel Model() {
    return {.voltage = 12.0, .source_resistance = 10.0, .sag_resistance = 20.0,
            .recovery_time_seconds = 0.1, .ripple_amplitude = 0.5,
            .ripple_frequency_hz = 100.0, .maximum_sag = 4.0,
            .maximum_load_current = 1.0};
}
}

TEST_CASE("M18-C10a dynamic supply BE sag and analytic branch derivative match reference") {
    const auto model = Model();
    constexpr double dt = 0.01;
    constexpr double current = -0.05;
    const auto evaluated = solo_rig::EvaluateDynamicPowerSupply(model, current, {}, dt);
    REQUIRE(evaluated.Succeeded());
    constexpr double alpha = 0.01 / 0.11;
    const double sag = alpha * 20.0 * 0.05;
    REQUIRE(std::fabs(evaluated.next_state.sag - sag) < 1.0e-15);
    REQUIRE(std::fabs(evaluated.target_voltage - (12.0 - sag)) < 1.0e-15);
    const auto plus = solo_rig::EvaluateDynamicPowerSupply(model, current + 1.0e-7, {}, dt);
    const auto minus = solo_rig::EvaluateDynamicPowerSupply(model, current - 1.0e-7, {}, dt);
    REQUIRE(std::fabs((plus.target_voltage - minus.target_voltage) / 2.0e-7 -
                      evaluated.target_voltage_derivative_current) < 1.0e-8);
}

TEST_CASE("M18-C10a dynamic supply covers recovery ripple clamps and finite boundaries") {
    const auto model = Model();
    const auto recovery = solo_rig::EvaluateDynamicPowerSupply(model, 0.0, {.sag = 2.0}, 0.1);
    REQUIRE(recovery.Succeeded());
    REQUIRE(std::fabs(recovery.next_state.sag - 1.0) < 1.0e-15);
    const auto ripple = solo_rig::EvaluateDynamicPowerSupply(
        model, 0.0, {.sag = 0.0, .ripple_phase = 3.14159265358979323846}, 0.001);
    REQUIRE(ripple.Succeeded());
    REQUIRE(std::fabs(ripple.target_voltage - 11.5) < 1.0e-12);
    const auto clamped = solo_rig::EvaluateDynamicPowerSupply(model, -1.0e308, {}, 1.0);
    REQUIRE(clamped.Succeeded());
    REQUIRE(clamped.next_state.sag == 4.0);
    REQUIRE(clamped.target_voltage_derivative_current == 0.0);
    REQUIRE(solo_rig::EvaluateDynamicPowerSupply(
        model, std::numeric_limits<double>::infinity(), {}, 0.01).error ==
        solo_rig::NonlinearDeviceError::NonFinite);
    auto invalid = model;
    invalid.maximum_sag = 13.0;
    REQUIRE(!solo_rig::ValidateDynamicPowerSupplyModel(invalid));
}
