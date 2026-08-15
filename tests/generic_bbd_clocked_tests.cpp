#include "dsp/generic_bbd_clocked.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>
#include <type_traits>

namespace {

solo_rig::GenericBbdClockedModel Model() {
    return {.bucket_count = 512U, .clock_frequency_hz = 96000.0,
        .minimum_clock_frequency_hz = 48000.0, .maximum_clock_frequency_hz = 192000.0,
        .clock_control_octaves = 1.0, .transfer_efficiency = 0.999, .feedback = 0.1,
        .wet = 1.0, .dry = 0.0, .maximum_input = 2.0};
}

} // namespace

TEST_CASE("M18-D3a generic clocked BBD follows explicit finite clock control") {
    solo_rig::GenericBbdClockedRuntime runtime;
    REQUIRE(runtime.Prepare(Model(), 192000.0, 4U) ==
        solo_rig::GenericBbdClockedError::None);
    REQUIRE(runtime.IsPrepared());
    REQUIRE(runtime.RingSize() > 2U);
    double output{};
    REQUIRE(runtime.Process(1.5, -1.0, output) == solo_rig::GenericBbdClockedError::None);
    REQUIRE(std::isfinite(output));
    REQUIRE(runtime.CurrentClockFrequencyHz() == 48000.0);
    REQUIRE(runtime.Process(0.0, 1.0, output) == solo_rig::GenericBbdClockedError::None);
    REQUIRE(runtime.CurrentClockFrequencyHz() == 192000.0);
    REQUIRE(std::isfinite(output));
    for (int index = 0; index < 4096; ++index) {
        REQUIRE(runtime.Process(index == 0 ? 1.0 : 0.0,
            index % 2 == 0 ? -0.25 : 0.25, output) ==
            solo_rig::GenericBbdClockedError::None);
        REQUIRE(std::isfinite(output));
    }
    runtime.Reset();
    double after_reset{};
    REQUIRE(runtime.Process(0.0, 0.0, after_reset) == solo_rig::GenericBbdClockedError::None);
    REQUIRE(after_reset == 0.0);
    REQUIRE(runtime.SetSampleRate(96000.0) == solo_rig::GenericBbdClockedError::None);
    REQUIRE(runtime.SetSampleRate(std::numeric_limits<double>::quiet_NaN()) ==
        solo_rig::GenericBbdClockedError::InvalidSampleRate);
    static_assert(noexcept(std::declval<solo_rig::GenericBbdClockedRuntime&>().Process(
        0.0, 0.0, std::declval<double&>())));
}

TEST_CASE("M18-D3a generic clocked BBD rejects unsafe inputs and models") {
    solo_rig::GenericBbdClockedRuntime runtime;
    double output{123.0};
    REQUIRE(runtime.Process(0.0, 0.0, output) ==
        solo_rig::GenericBbdClockedError::NotPrepared);
    REQUIRE(output == 0.0);
    REQUIRE(runtime.Prepare(Model(), 192000.0, 0U) ==
        solo_rig::GenericBbdClockedError::InvalidOversampling);
    auto invalid = Model();
    invalid.minimum_clock_frequency_hz = 0.0;
    REQUIRE(runtime.Prepare(invalid, 48000.0, 1U) ==
        solo_rig::GenericBbdClockedError::InvalidModel);
    REQUIRE(runtime.Prepare(Model(), 192000.0, 1U) == solo_rig::GenericBbdClockedError::None);
    REQUIRE(runtime.Process(std::numeric_limits<double>::infinity(), 0.0, output) ==
        solo_rig::GenericBbdClockedError::NonFinite);
    REQUIRE(runtime.Process(0.0, std::numeric_limits<double>::quiet_NaN(), output) ==
        solo_rig::GenericBbdClockedError::NonFinite);
}
