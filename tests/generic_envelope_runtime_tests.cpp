#include "dsp/generic_envelope.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>
#include <type_traits>

namespace {

solo_rig::GenericEnvelopeModel Model() {
    return {.input_gain = 1.0,
        .attack_seconds = 0.01,
        .release_seconds = 0.03,
        .detector_ceiling = 2.0,
        .output_scale = 1.0,
        .maximum_input = 2.0,
        .maximum_output = 2.0,
        .initial_envelope = 0.0};
}

} // namespace

TEST_CASE("M18-D2c1 generic envelope attack and release match backward Euler") {
    solo_rig::GenericEnvelopeRuntime runtime;
    REQUIRE(runtime.Prepare(Model(), 100.0, 1U) == solo_rig::GenericEnvelopeError::None);
    double output{};
    REQUIRE(runtime.Process(1.0, output) == solo_rig::GenericEnvelopeError::None);
    REQUIRE(std::fabs(runtime.CurrentEnvelope() - 0.5) < 1.0e-12);
    REQUIRE(runtime.Process(1.0, output) == solo_rig::GenericEnvelopeError::None);
    REQUIRE(std::fabs(runtime.CurrentEnvelope() - 0.75) < 1.0e-12);

    auto release = Model();
    release.attack_seconds = 0.0;
    release.initial_envelope = 1.0;
    REQUIRE(runtime.Prepare(release, 100.0, 1U) == solo_rig::GenericEnvelopeError::None);
    REQUIRE(runtime.Process(0.0, output) == solo_rig::GenericEnvelopeError::None);
    REQUIRE(std::fabs(runtime.CurrentEnvelope() - 0.75) < 1.0e-12);
}

TEST_CASE("M18-D2c1 generic envelope advances every oversampling substep") {
    solo_rig::GenericEnvelopeRuntime runtime;
    REQUIRE(runtime.Prepare(Model(), 100.0, 4U) == solo_rig::GenericEnvelopeError::None);
    double output{};
    REQUIRE(runtime.Process(1.0, output) == solo_rig::GenericEnvelopeError::None);
    REQUIRE(std::fabs(runtime.CurrentEnvelope() - 0.5904) < 1.0e-12);
}

TEST_CASE("M18-D2c1 generic envelope scales clamps and resets its output state") {
    auto model = Model();
    model.attack_seconds = 0.0;
    model.output_scale = 3.0;
    model.maximum_output = 1.0;
    model.initial_envelope = 0.25;
    solo_rig::GenericEnvelopeRuntime runtime;
    REQUIRE(runtime.Prepare(model, 48000.0, 1U) == solo_rig::GenericEnvelopeError::None);
    double output{};
    REQUIRE(runtime.Process(1.0, output) == solo_rig::GenericEnvelopeError::None);
    REQUIRE(output == 1.0);
    runtime.Reset();
    REQUIRE(runtime.CurrentEnvelope() == 0.25);
    REQUIRE(runtime.SetSampleRate(96000.0) == solo_rig::GenericEnvelopeError::None);
    REQUIRE(runtime.CurrentEnvelope() == 0.25);
    REQUIRE(runtime.SetSampleRate(std::numeric_limits<double>::quiet_NaN()) ==
            solo_rig::GenericEnvelopeError::InvalidSampleRate);
    REQUIRE(runtime.CurrentEnvelope() == 0.25);
}

TEST_CASE("M18-D2c1 generic envelope runtime is finite at 192 kHz 4x") {
    solo_rig::GenericEnvelopeRuntime runtime;
    REQUIRE(runtime.Prepare(Model(), 192000.0, 4U) == solo_rig::GenericEnvelopeError::None);
    double output{};
    for (int index = 0; index < 4096; ++index) {
        REQUIRE(runtime.Process(index % 2 == 0 ? 1.5 : -0.5, output) ==
                solo_rig::GenericEnvelopeError::None);
        REQUIRE(std::isfinite(output));
        REQUIRE(output >= 0.0 && output <= 2.0);
    }
    REQUIRE(runtime.Process(std::numeric_limits<double>::infinity(), output) ==
            solo_rig::GenericEnvelopeError::NonFinite);
    static_assert(noexcept(std::declval<solo_rig::GenericEnvelopeRuntime&>().Process(
        0.0, std::declval<double&>())));
}

TEST_CASE("M18-D2c1 generic envelope runtime rejects unsafe preparation") {
    solo_rig::GenericEnvelopeRuntime runtime;
    double output{123.0};
    REQUIRE(runtime.Process(0.0, output) == solo_rig::GenericEnvelopeError::NotPrepared);
    REQUIRE(output == 0.0);
    REQUIRE(runtime.Prepare(Model(), 0.0, 1U) ==
            solo_rig::GenericEnvelopeError::InvalidSampleRate);
    REQUIRE(runtime.Prepare(Model(), 48000.0, 0U) ==
            solo_rig::GenericEnvelopeError::InvalidOversampling);
    REQUIRE(runtime.Prepare(Model(), 48000.0, 17U) ==
            solo_rig::GenericEnvelopeError::InvalidOversampling);
    REQUIRE(runtime.SetSampleRate(48000.0) == solo_rig::GenericEnvelopeError::NotPrepared);
}
