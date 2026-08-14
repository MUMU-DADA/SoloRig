#include "dsp/generic_bbd.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>

namespace {

solo_rig::GenericBbdModel Model() {
    return {.bucket_count = 4U, .clock_frequency_hz = 48000.0,
        .transfer_efficiency = 1.0, .feedback = 0.0, .wet = 1.0, .dry = 0.0,
        .maximum_input = 1.0};
}

} // namespace

TEST_CASE("M18-D2a generic BBD delay matches the clocked bucket reference") {
    solo_rig::GenericBbdRuntime runtime;
    REQUIRE(runtime.Prepare(Model(), 48000.0, 1U) == solo_rig::GenericBbdError::None);
    REQUIRE(std::fabs(runtime.DelaySeconds() - (4.0 / 96000.0)) < 1.0e-15);
    double output{};
    REQUIRE(runtime.Process(0.0, output) == solo_rig::GenericBbdError::None);
    REQUIRE(output == 0.0);
    REQUIRE(runtime.Process(1.0, output) == solo_rig::GenericBbdError::None);
    REQUIRE(output == 0.0);
    REQUIRE(runtime.Process(0.0, output) == solo_rig::GenericBbdError::None);
    REQUIRE(output == 0.0);
    REQUIRE(runtime.Process(0.0, output) == solo_rig::GenericBbdError::None);
    REQUIRE(std::fabs(output - 1.0) < 1.0e-12);
}

TEST_CASE("M18-D2a generic BBD efficiency feedback and input clamp are bounded") {
    auto model = Model();
    model.transfer_efficiency = 0.5;
    model.feedback = 0.5;
    model.wet = 1.0;
    solo_rig::GenericBbdRuntime runtime;
    REQUIRE(runtime.Prepare(model, 48000.0, 1U) == solo_rig::GenericBbdError::None);
    double output{};
    for (int index = 0; index < 32; ++index) {
        REQUIRE(runtime.Process(index == 0 ? 10.0 : 0.0, output) == solo_rig::GenericBbdError::None);
        REQUIRE(std::isfinite(output));
    }
    REQUIRE(output <= 1.0e-6);
    REQUIRE(runtime.Process(std::numeric_limits<double>::quiet_NaN(), output) ==
            solo_rig::GenericBbdError::NonFinite);
    auto invalid = model;
    invalid.feedback = 1.0;
    REQUIRE(!solo_rig::ValidateGenericBbdModel(invalid));
    invalid = model;
    invalid.bucket_count = 0U;
    REQUIRE(!solo_rig::ValidateGenericBbdModel(invalid));
}
