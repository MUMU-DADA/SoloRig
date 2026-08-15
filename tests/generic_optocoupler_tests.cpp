#include "dsp/generic_optocoupler.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>

namespace {

solo_rig::GenericOptocouplerModel Model() {
    return {.dark_resistance_ohms = 1.0e6,
        .lit_resistance_ohms = 100.0,
        .emitter_threshold_volts = 1.0,
        .emitter_scale_volts = 4.0,
        .response_exponent = 2.0,
        .attack_seconds = 0.01,
        .release_seconds = 0.03,
        .maximum_drive_volts = 5.0,
        .initial_illumination = 0.0};
}

solo_rig::GenericOptocouplerEvaluation Evaluate(double drive, double illumination) {
    solo_rig::GenericOptocouplerEvaluation result{};
    REQUIRE(solo_rig::EvaluateGenericOptocoupler(Model(), drive, illumination, result) ==
            solo_rig::GenericOptocouplerError::None);
    return result;
}

} // namespace

TEST_CASE("M18-D2d1 generic optocoupler maps bounded emitter drive to illumination") {
    REQUIRE(Evaluate(-10.0, 0.0).target_illumination == 0.0);
    REQUIRE(Evaluate(1.0, 0.0).target_illumination == 0.0);
    REQUIRE(std::fabs(Evaluate(3.0, 0.0).target_illumination - 0.25) < 1.0e-12);
    REQUIRE(Evaluate(5.0, 0.0).target_illumination == 1.0);
    REQUIRE(Evaluate(50.0, 0.0).target_illumination == 1.0);
}

TEST_CASE("M18-D2d1 generic optocoupler interpolates LDR resistance logarithmically") {
    REQUIRE(Evaluate(0.0, 0.0).resistance_ohms == 1.0e6);
    REQUIRE(std::fabs(Evaluate(0.0, 0.5).resistance_ohms - 1.0e4) < 1.0e-9);
    REQUIRE(Evaluate(0.0, 1.0).resistance_ohms == 100.0);
}

TEST_CASE("M18-D2d1 generic optocoupler rejects invalid and nonfinite contracts") {
    auto invalid = Model();
    invalid.lit_resistance_ohms = invalid.dark_resistance_ohms + 1.0;
    REQUIRE(!solo_rig::ValidateGenericOptocouplerModel(invalid));
    invalid = Model();
    invalid.emitter_threshold_volts = invalid.maximum_drive_volts + 1.0;
    REQUIRE(!solo_rig::ValidateGenericOptocouplerModel(invalid));
    invalid = Model();
    invalid.response_exponent = 0.0;
    REQUIRE(!solo_rig::ValidateGenericOptocouplerModel(invalid));
    invalid = Model();
    invalid.initial_illumination = std::numeric_limits<double>::infinity();
    REQUIRE(!solo_rig::ValidateGenericOptocouplerModel(invalid));

    solo_rig::GenericOptocouplerEvaluation output{1.0, 1.0};
    REQUIRE(solo_rig::EvaluateGenericOptocoupler(
        Model(), std::numeric_limits<double>::quiet_NaN(), 0.0, output) ==
        solo_rig::GenericOptocouplerError::NonFinite);
    REQUIRE(output.target_illumination == 0.0);
    REQUIRE(output.resistance_ohms == 0.0);
    REQUIRE(solo_rig::EvaluateGenericOptocoupler(Model(), 1.0,
        std::numeric_limits<double>::infinity(), output) ==
        solo_rig::GenericOptocouplerError::NonFinite);
}

TEST_CASE("M18-D2d1 generic optocoupler remains finite at declared extremes") {
    auto extreme = Model();
    extreme.dark_resistance_ohms = 1.0e12;
    extreme.lit_resistance_ohms = 1.0e-3;
    extreme.emitter_threshold_volts = 0.0;
    extreme.emitter_scale_volts = 1.0e-6;
    extreme.response_exponent = 10.0;
    extreme.maximum_drive_volts = 1.0e6;
    solo_rig::GenericOptocouplerEvaluation output{};
    REQUIRE(solo_rig::EvaluateGenericOptocoupler(extreme, 1.0e300, 0.5, output) ==
            solo_rig::GenericOptocouplerError::None);
    REQUIRE(output.target_illumination == 1.0);
    REQUIRE(std::isfinite(output.resistance_ohms));
    REQUIRE(output.resistance_ohms >= extreme.lit_resistance_ohms);
    REQUIRE(output.resistance_ohms <= extreme.dark_resistance_ohms);
}
