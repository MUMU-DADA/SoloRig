#include "dsp/generic_vca.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>
#include <type_traits>

namespace {

constexpr double kDoubleGainDb = 6.020599913279624;

solo_rig::GenericVcaModel Model() {
    return {.reference_control_voltage = 0.0,
        .reference_gain = 1.0,
        .gain_decibels_per_volt = kDoubleGainDb,
        .minimum_gain = 0.1,
        .maximum_gain = 4.0,
        .control_smoothing_seconds = 0.0,
        .initial_control_voltage = 0.0,
        .maximum_control_voltage = 2.0,
        .maximum_input = 2.0,
        .maximum_output = 3.0};
}

} // namespace

TEST_CASE("M18-D2b2 generic VCA runtime applies input gain and output clamps") {
    solo_rig::GenericVcaRuntime runtime;
    REQUIRE(runtime.Prepare(Model(), 48000.0, 1U) == solo_rig::GenericVcaError::None);
    double output{};
    REQUIRE(runtime.Process(2.0, 1.0, output) == solo_rig::GenericVcaError::None);
    REQUIRE(output == 3.0);
    REQUIRE(std::fabs(runtime.CurrentGain() - 2.0) < 1.0e-12);
    REQUIRE(runtime.Process(-100.0, 0.0, output) == solo_rig::GenericVcaError::None);
    REQUIRE(output == -2.0);
}

TEST_CASE("M18-D2b2 generic VCA backward-Euler smoothing advances every substep") {
    auto model = Model();
    model.control_smoothing_seconds = 0.01;
    solo_rig::GenericVcaRuntime runtime;
    REQUIRE(runtime.Prepare(model, 100.0, 1U) == solo_rig::GenericVcaError::None);
    double output{};
    REQUIRE(runtime.Process(1.0, 1.0, output) == solo_rig::GenericVcaError::None);
    REQUIRE(std::fabs(runtime.CurrentGain() - 1.5) < 1.0e-12);
    REQUIRE(std::fabs(output - 1.5) < 1.0e-12);

    REQUIRE(runtime.Prepare(model, 100.0, 4U) == solo_rig::GenericVcaError::None);
    REQUIRE(runtime.Process(1.0, 1.0, output) == solo_rig::GenericVcaError::None);
    REQUIRE(std::fabs(runtime.CurrentGain() - 1.5904) < 1.0e-12);
}

TEST_CASE("M18-D2b2 generic VCA runtime is Reset-safe and finite at 192 kHz 4x") {
    auto model = Model();
    model.control_smoothing_seconds = 0.002;
    model.initial_control_voltage = -1.0;
    solo_rig::GenericVcaRuntime runtime;
    REQUIRE(runtime.Prepare(model, 192000.0, 4U) == solo_rig::GenericVcaError::None);
    double output{};
    for (int index = 0; index < 4096; ++index) {
        REQUIRE(runtime.Process(index % 2 == 0 ? 1.5 : -1.5,
                                index % 3 == 0 ? 1.0 : -1.0, output) ==
                solo_rig::GenericVcaError::None);
        REQUIRE(std::isfinite(output));
        REQUIRE(std::isfinite(runtime.CurrentGain()));
    }
    runtime.Reset();
    REQUIRE(std::fabs(runtime.CurrentGain() - 0.5) < 1.0e-12);
    REQUIRE(runtime.SetSampleRate(96000.0) == solo_rig::GenericVcaError::None);
    REQUIRE(std::fabs(runtime.CurrentGain() - 0.5) < 1.0e-12);
    REQUIRE(runtime.SetSampleRate(std::numeric_limits<double>::quiet_NaN()) ==
            solo_rig::GenericVcaError::InvalidSampleRate);
    REQUIRE(runtime.Process(std::numeric_limits<double>::infinity(), 0.0, output) ==
            solo_rig::GenericVcaError::NonFinite);
    REQUIRE(runtime.Process(0.0, std::numeric_limits<double>::quiet_NaN(), output) ==
            solo_rig::GenericVcaError::NonFinite);
    static_assert(noexcept(std::declval<solo_rig::GenericVcaRuntime&>().Process(
        0.0, 0.0, std::declval<double&>())));
}

TEST_CASE("M18-D2b2 generic VCA runtime rejects unsafe preparation") {
    solo_rig::GenericVcaRuntime runtime;
    double output{123.0};
    REQUIRE(runtime.Process(0.0, 0.0, output) == solo_rig::GenericVcaError::NotPrepared);
    REQUIRE(output == 0.0);
    REQUIRE(runtime.Prepare(Model(), 0.0, 1U) ==
            solo_rig::GenericVcaError::InvalidSampleRate);
    REQUIRE(runtime.Prepare(Model(), 48000.0, 0U) ==
            solo_rig::GenericVcaError::InvalidOversampling);
    auto invalid = Model();
    invalid.maximum_output = 0.0;
    REQUIRE(runtime.Prepare(invalid, 48000.0, 1U) ==
            solo_rig::GenericVcaError::InvalidModel);
}
