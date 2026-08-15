#include "dsp/generic_vca.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>

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

TEST_CASE("M18-D2b2 generic VCA target gain follows the decibel control law") {
    double gain{};
    REQUIRE(solo_rig::EvaluateGenericVcaTargetGain(Model(), 0.0, gain) ==
            solo_rig::GenericVcaError::None);
    REQUIRE(std::fabs(gain - 1.0) < 1.0e-12);
    REQUIRE(solo_rig::EvaluateGenericVcaTargetGain(Model(), 1.0, gain) ==
            solo_rig::GenericVcaError::None);
    REQUIRE(std::fabs(gain - 2.0) < 1.0e-12);
    REQUIRE(solo_rig::EvaluateGenericVcaTargetGain(Model(), -1.0, gain) ==
            solo_rig::GenericVcaError::None);
    REQUIRE(std::fabs(gain - 0.5) < 1.0e-12);

    auto inverse = Model();
    inverse.gain_decibels_per_volt = -kDoubleGainDb;
    REQUIRE(solo_rig::EvaluateGenericVcaTargetGain(inverse, 1.0, gain) ==
            solo_rig::GenericVcaError::None);
    REQUIRE(std::fabs(gain - 0.5) < 1.0e-12);
}

TEST_CASE("M18-D2b2 generic VCA clamps control exponent and declared gain") {
    auto model = Model();
    model.gain_decibels_per_volt = 240.0;
    double gain{};
    REQUIRE(solo_rig::EvaluateGenericVcaTargetGain(model, 1.0e6, gain) ==
            solo_rig::GenericVcaError::None);
    REQUIRE(gain == model.maximum_gain);
    REQUIRE(solo_rig::EvaluateGenericVcaTargetGain(model, -1.0e6, gain) ==
            solo_rig::GenericVcaError::None);
    REQUIRE(gain == model.minimum_gain);
}

TEST_CASE("M18-D2b2 generic VCA rejects invalid and nonfinite contracts") {
    auto invalid = Model();
    invalid.minimum_gain = 2.0;
    REQUIRE(!solo_rig::ValidateGenericVcaModel(invalid));
    invalid = Model();
    invalid.maximum_input = 0.0;
    REQUIRE(!solo_rig::ValidateGenericVcaModel(invalid));
    invalid = Model();
    invalid.initial_control_voltage = 3.0;
    REQUIRE(!solo_rig::ValidateGenericVcaModel(invalid));
    invalid = Model();
    invalid.reference_gain = std::numeric_limits<double>::infinity();
    REQUIRE(!solo_rig::ValidateGenericVcaModel(invalid));

    double gain{123.0};
    REQUIRE(solo_rig::EvaluateGenericVcaTargetGain(
        Model(), std::numeric_limits<double>::quiet_NaN(), gain) ==
        solo_rig::GenericVcaError::NonFinite);
    REQUIRE(gain == 0.0);
}
