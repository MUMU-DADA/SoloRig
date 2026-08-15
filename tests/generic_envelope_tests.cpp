#include "dsp/generic_envelope.hpp"
#include "test_framework.hpp"

#include <limits>

namespace {

solo_rig::GenericEnvelopeModel Model() {
    return {.input_gain = 2.0,
        .attack_seconds = 0.01,
        .release_seconds = 0.03,
        .detector_ceiling = 1.0,
        .output_scale = 2.0,
        .maximum_input = 0.75,
        .maximum_output = 1.5,
        .initial_envelope = 0.25};
}

} // namespace

TEST_CASE("M18-D2c1 generic envelope target is full-wave symmetric") {
    double target{};
    REQUIRE(solo_rig::EvaluateGenericEnvelopeTarget(Model(), 0.25, target) ==
            solo_rig::GenericEnvelopeError::None);
    REQUIRE(target == 0.5);
    REQUIRE(solo_rig::EvaluateGenericEnvelopeTarget(Model(), -0.25, target) ==
            solo_rig::GenericEnvelopeError::None);
    REQUIRE(target == 0.5);
}

TEST_CASE("M18-D2c1 generic envelope clamps input and detector magnitude") {
    double target{};
    REQUIRE(solo_rig::EvaluateGenericEnvelopeTarget(Model(), 1.0e300, target) ==
            solo_rig::GenericEnvelopeError::None);
    REQUIRE(target == 1.0);
    REQUIRE(solo_rig::EvaluateGenericEnvelopeTarget(Model(), -1.0e300, target) ==
            solo_rig::GenericEnvelopeError::None);
    REQUIRE(target == 1.0);
}

TEST_CASE("M18-D2c1 generic envelope rejects invalid and nonfinite boundaries") {
    auto invalid = Model();
    invalid.initial_envelope = 2.0;
    REQUIRE(!solo_rig::ValidateGenericEnvelopeModel(invalid));
    invalid = Model();
    invalid.attack_seconds = 61.0;
    REQUIRE(!solo_rig::ValidateGenericEnvelopeModel(invalid));
    invalid = Model();
    invalid.maximum_output = 0.0;
    REQUIRE(!solo_rig::ValidateGenericEnvelopeModel(invalid));
    invalid = Model();
    invalid.input_gain = std::numeric_limits<double>::infinity();
    REQUIRE(!solo_rig::ValidateGenericEnvelopeModel(invalid));

    double target{123.0};
    REQUIRE(solo_rig::EvaluateGenericEnvelopeTarget(
        Model(), std::numeric_limits<double>::quiet_NaN(), target) ==
        solo_rig::GenericEnvelopeError::NonFinite);
    REQUIRE(target == 0.0);
}
