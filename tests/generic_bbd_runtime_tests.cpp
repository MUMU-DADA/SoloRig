#include "dsp/generic_bbd.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>
#include <type_traits>

TEST_CASE("M18-D2a generic BBD runtime is preallocated at 192 kHz 4x and Reset-safe") {
    const solo_rig::GenericBbdModel model{.bucket_count = 512U, .clock_frequency_hz = 96000.0,
        .transfer_efficiency = 0.999, .feedback = 0.25, .wet = 1.0, .dry = 0.0,
        .maximum_input = 2.0};
    solo_rig::GenericBbdRuntime runtime;
    REQUIRE(runtime.Prepare(model, 192000.0, 4U) == solo_rig::GenericBbdError::None);
    REQUIRE(runtime.IsPrepared());
    REQUIRE(runtime.RingSize() > 2U);
    double output{};
    for (int index = 0; index < 4096; ++index) {
        REQUIRE(runtime.Process(index == 0 ? 1.5 : 0.0, output) == solo_rig::GenericBbdError::None);
        REQUIRE(std::isfinite(output));
    }
    runtime.Reset();
    double after_reset{};
    REQUIRE(runtime.Process(0.0, after_reset) == solo_rig::GenericBbdError::None);
    REQUIRE(after_reset == 0.0);
    REQUIRE(runtime.SetSampleRate(96000.0) == solo_rig::GenericBbdError::None);
    REQUIRE(runtime.SetSampleRate(std::numeric_limits<double>::quiet_NaN()) ==
            solo_rig::GenericBbdError::InvalidSampleRate);
    REQUIRE(runtime.Process(std::numeric_limits<double>::infinity(), output) ==
            solo_rig::GenericBbdError::NonFinite);
    static_assert(noexcept(std::declval<solo_rig::GenericBbdRuntime&>().Process(
        0.0, std::declval<double&>())));
}

TEST_CASE("M18-D2a generic BBD runtime rejects unsafe prepare boundaries") {
    solo_rig::GenericBbdRuntime runtime;
    REQUIRE(runtime.Prepare({.bucket_count = 0U, .clock_frequency_hz = 48000.0,
        .transfer_efficiency = 1.0, .feedback = 0.0, .wet = 1.0, .dry = 0.0,
        .maximum_input = 1.0}, 48000.0, 1U) == solo_rig::GenericBbdError::InvalidModel);
    REQUIRE(runtime.Prepare({.bucket_count = 1U, .clock_frequency_hz = 48000.0,
        .transfer_efficiency = 1.0, .feedback = 0.0, .wet = 1.0, .dry = 0.0,
        .maximum_input = 1.0}, 48000.0, 0U) == solo_rig::GenericBbdError::InvalidOversampling);
    REQUIRE(runtime.Prepare({.bucket_count = 1U, .clock_frequency_hz = 1.0,
        .transfer_efficiency = 1.0, .feedback = 0.0, .wet = 1.0, .dry = 0.0,
        .maximum_input = 1.0}, 1.0, 16U) == solo_rig::GenericBbdError::None);
}
