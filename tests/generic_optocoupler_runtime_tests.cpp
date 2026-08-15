#include "dsp/generic_optocoupler.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>
#include <type_traits>

namespace {

solo_rig::GenericOptocouplerModel Model() {
    return {.dark_resistance_ohms = 1.0e6,
        .lit_resistance_ohms = 100.0,
        .emitter_threshold_volts = 1.0,
        .emitter_scale_volts = 4.0,
        .response_exponent = 1.0,
        .attack_seconds = 0.01,
        .release_seconds = 0.03,
        .maximum_drive_volts = 5.0,
        .initial_illumination = 0.0};
}

} // namespace

TEST_CASE("M18-D2d1 generic optocoupler attack and release match backward Euler") {
    solo_rig::GenericOptocouplerRuntime runtime;
    REQUIRE(runtime.Prepare(Model(), 100.0, 1U) ==
            solo_rig::GenericOptocouplerError::None);
    double resistance{};
    REQUIRE(runtime.Process(5.0, resistance) == solo_rig::GenericOptocouplerError::None);
    REQUIRE(std::fabs(runtime.CurrentIllumination() - 0.5) < 1.0e-12);
    REQUIRE(std::fabs(resistance - 1.0e4) < 1.0e-9);
    REQUIRE(runtime.Process(5.0, resistance) == solo_rig::GenericOptocouplerError::None);
    REQUIRE(std::fabs(runtime.CurrentIllumination() - 0.75) < 1.0e-12);

    auto release = Model();
    release.attack_seconds = 0.0;
    release.initial_illumination = 1.0;
    REQUIRE(runtime.Prepare(release, 100.0, 1U) ==
            solo_rig::GenericOptocouplerError::None);
    REQUIRE(runtime.Process(0.0, resistance) == solo_rig::GenericOptocouplerError::None);
    REQUIRE(std::fabs(runtime.CurrentIllumination() - 0.75) < 1.0e-12);
}

TEST_CASE("M18-D2d1 generic optocoupler advances every oversampling substep") {
    solo_rig::GenericOptocouplerRuntime runtime;
    REQUIRE(runtime.Prepare(Model(), 100.0, 4U) ==
            solo_rig::GenericOptocouplerError::None);
    double resistance{};
    REQUIRE(runtime.Process(5.0, resistance) == solo_rig::GenericOptocouplerError::None);
    REQUIRE(std::fabs(runtime.CurrentIllumination() - 0.5904) < 1.0e-12);
}

TEST_CASE("M18-D2d1 generic optocoupler Reset and sample-rate changes restore initial state") {
    auto model = Model();
    model.initial_illumination = 0.25;
    solo_rig::GenericOptocouplerRuntime runtime;
    REQUIRE(runtime.Prepare(model, 48000.0, 1U) ==
            solo_rig::GenericOptocouplerError::None);
    double resistance{};
    REQUIRE(runtime.Process(5.0, resistance) == solo_rig::GenericOptocouplerError::None);
    runtime.Reset();
    REQUIRE(runtime.CurrentIllumination() == 0.25);
    REQUIRE(runtime.SetSampleRate(96000.0) == solo_rig::GenericOptocouplerError::None);
    REQUIRE(runtime.CurrentIllumination() == 0.25);
    REQUIRE(runtime.SetSampleRate(std::numeric_limits<double>::quiet_NaN()) ==
            solo_rig::GenericOptocouplerError::InvalidSampleRate);
    REQUIRE(runtime.CurrentIllumination() == 0.25);
}

TEST_CASE("M18-D2d1 generic optocoupler runtime is finite at 192 kHz 4x") {
    solo_rig::GenericOptocouplerRuntime runtime;
    REQUIRE(runtime.Prepare(Model(), 192000.0, 4U) ==
            solo_rig::GenericOptocouplerError::None);
    double resistance{};
    for (int index = 0; index < 4096; ++index) {
        const double drive = index % 2 == 0 ? 1.0e300 : -1.0e300;
        REQUIRE(runtime.Process(drive, resistance) ==
                solo_rig::GenericOptocouplerError::None);
        REQUIRE(std::isfinite(resistance));
        REQUIRE(resistance >= 100.0 && resistance <= 1.0e6);
        REQUIRE(runtime.CurrentIllumination() >= 0.0);
        REQUIRE(runtime.CurrentIllumination() <= 1.0);
    }
    REQUIRE(runtime.Process(std::numeric_limits<double>::infinity(), resistance) ==
            solo_rig::GenericOptocouplerError::NonFinite);
    REQUIRE(resistance == 0.0);
    static_assert(noexcept(std::declval<solo_rig::GenericOptocouplerRuntime&>().Process(
        0.0, std::declval<double&>())));
}

TEST_CASE("M18-D2d1 generic optocoupler runtime rejects unsafe preparation") {
    solo_rig::GenericOptocouplerRuntime runtime;
    double resistance{123.0};
    REQUIRE(runtime.Process(0.0, resistance) ==
            solo_rig::GenericOptocouplerError::NotPrepared);
    REQUIRE(resistance == 0.0);
    REQUIRE(runtime.Prepare(Model(), 0.0, 1U) ==
            solo_rig::GenericOptocouplerError::InvalidSampleRate);
    REQUIRE(runtime.Prepare(Model(), 48000.0, 0U) ==
            solo_rig::GenericOptocouplerError::InvalidOversampling);
    REQUIRE(runtime.Prepare(Model(), 48000.0, 17U) ==
            solo_rig::GenericOptocouplerError::InvalidOversampling);
    REQUIRE(runtime.SetSampleRate(48000.0) ==
            solo_rig::GenericOptocouplerError::NotPrepared);
}
