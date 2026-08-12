#include "core/quality_policy.hpp"
#include "test_framework.hpp"

using solo_rig::CalculateOversamplingPlan;
using solo_rig::QualityMode;

TEST_CASE("Maximum quality reaches 192 kHz from a 48 kHz nonlinear module") {
    const auto plan = CalculateOversamplingPlan(48000.0, QualityMode::Maximum, true);

    REQUIRE(plan.factor == 4);
    REQUIRE(plan.processing_sample_rate_hz == 192000.0);
}

TEST_CASE("Maximum quality never exceeds 192 kHz for host formats") {
    const auto plan = CalculateOversamplingPlan(96000.0, QualityMode::Maximum, true);

    REQUIRE(plan.factor == 2);
    REQUIRE(plan.processing_sample_rate_hz == 192000.0);
}

TEST_CASE("Linear modules do not pay an unnecessary oversampling cost") {
    const auto plan = CalculateOversamplingPlan(48000.0, QualityMode::Maximum, false);

    REQUIRE(plan.factor == 1);
    REQUIRE(plan.processing_sample_rate_hz == 48000.0);
}

TEST_CASE("Draft quality keeps nonlinear modules at the host sample rate") {
    const auto plan = CalculateOversamplingPlan(44100.0, QualityMode::Draft, true);

    REQUIRE(plan.factor == 1);
    REQUIRE(plan.processing_sample_rate_hz == 44100.0);
}
