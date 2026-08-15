#include "dsp/generic_bbd_feedback.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>
#include <type_traits>

namespace {

solo_rig::GenericBbdFeedbackModel Model() {
    return {.bucket_count = 512U, .clock_frequency_hz = 96000.0,
        .minimum_clock_frequency_hz = 48000.0, .maximum_clock_frequency_hz = 192000.0,
        .clock_control_octaves = 1.0, .transfer_efficiency = 0.999,
        .feedback_gain = 0.5, .wet = 1.0, .dry = 0.0, .maximum_input = 2.0};
}

} // namespace

TEST_CASE("M18-D3b generic BBD feedback commits only the previous output state") {
    solo_rig::GenericBbdFeedbackRuntime runtime;
    REQUIRE(runtime.Prepare(Model(), 192000.0, 4U) ==
        solo_rig::GenericBbdFeedbackError::None);
    double output{};
    REQUIRE(runtime.Process(1.5, 0.0, output) == solo_rig::GenericBbdFeedbackError::None);
    REQUIRE(output == 0.0);
    REQUIRE(runtime.PreviousOutput() == output);
    bool feedback_seen = false;
    for (int index = 0; index < 4096; ++index) {
        REQUIRE(runtime.Process(0.0, index % 2 == 0 ? -0.25 : 0.25, output) ==
            solo_rig::GenericBbdFeedbackError::None);
        REQUIRE(std::isfinite(output));
        if (std::fabs(runtime.PreviousOutput()) > 1.0e-12) feedback_seen = true;
    }
    REQUIRE(feedback_seen);
    runtime.Reset();
    REQUIRE(runtime.PreviousOutput() == 0.0);
    REQUIRE(runtime.Process(0.0, 0.0, output) == solo_rig::GenericBbdFeedbackError::None);
    REQUIRE(output == 0.0);
    REQUIRE(runtime.SetSampleRate(96000.0) == solo_rig::GenericBbdFeedbackError::None);
    static_assert(noexcept(std::declval<solo_rig::GenericBbdFeedbackRuntime&>().Process(
        0.0, 0.0, std::declval<double&>())));
}

TEST_CASE("M18-D3b generic BBD feedback is deterministic and rejects unsafe gains") {
    solo_rig::GenericBbdFeedbackRuntime runtime;
    auto invalid = Model();
    invalid.feedback_gain = 1.0;
    REQUIRE(runtime.Prepare(invalid, 48000.0, 1U) ==
        solo_rig::GenericBbdFeedbackError::InvalidModel);
    REQUIRE(runtime.Prepare(Model(), 48000.0, 0U) ==
        solo_rig::GenericBbdFeedbackError::InvalidOversampling);
    REQUIRE(runtime.Prepare(Model(), 48000.0, 1U) == solo_rig::GenericBbdFeedbackError::None);
    double output{123.0};
    REQUIRE(runtime.Process(std::numeric_limits<double>::infinity(), 0.0, output) ==
        solo_rig::GenericBbdFeedbackError::NonFinite);
    REQUIRE(runtime.Process(0.0, std::numeric_limits<double>::quiet_NaN(), output) ==
        solo_rig::GenericBbdFeedbackError::NonFinite);
    REQUIRE(runtime.SetSampleRate(std::numeric_limits<double>::infinity()) ==
        solo_rig::GenericBbdFeedbackError::InvalidSampleRate);
}
