#include "../fixtures/whitebox/m19a2_feedback_fixtures.hpp"
#include "content/behavior_feedback_runtime.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>

TEST_CASE("M19-A2 validates the self-authored generic single delayed feedback chain") {
    const auto fixture = solo_rig::m19a2_fixtures::VcoBbdSingleFeedback();
    const auto compiled = solo_rig::CompileVcoBbdFeedbackGraph(
        fixture.nodes, fixture.routes);
    REQUIRE(compiled.error == solo_rig::BehaviorFeedbackCompileError::None);
    REQUIRE(compiled.plan.has_value());
    solo_rig::TypedVcoBbdFeedbackRuntime runtime;
    REQUIRE(runtime.Prepare(*compiled.plan, 192000.0, 4U) ==
        solo_rig::TypedVcoBbdFeedbackRuntimeError::None);
    double output{};
    for (int index = 0; index < 2048; ++index) {
        REQUIRE(runtime.Process(index == 0 ? 1.0 : 0.0, output) ==
            solo_rig::TypedVcoBbdFeedbackRuntimeError::None);
        REQUIRE(std::isfinite(output));
    }
    runtime.Reset();
    REQUIRE(runtime.Process(0.0, output) ==
        solo_rig::TypedVcoBbdFeedbackRuntimeError::None);
    REQUIRE(output == 0.0);
}

TEST_CASE("M19-A2 rejects implicit feedback substitutions and nonfinite input") {
    auto fixture = solo_rig::m19a2_fixtures::VcoBbdSingleFeedback();
    fixture.routes[2].destination_port = "audio_in";
    REQUIRE(solo_rig::CompileVcoBbdFeedbackGraph(fixture.nodes, fixture.routes).error ==
        solo_rig::BehaviorFeedbackCompileError::ZeroDelayCycle);
    fixture = solo_rig::m19a2_fixtures::VcoBbdSingleFeedback();
    fixture.routes[2].source_port = "feedback";
    REQUIRE(solo_rig::CompileVcoBbdFeedbackGraph(fixture.nodes, fixture.routes).error ==
        solo_rig::BehaviorFeedbackCompileError::UnknownPort);
    fixture = solo_rig::m19a2_fixtures::VcoBbdSingleFeedback();
    const auto plan = solo_rig::CompileVcoBbdFeedbackGraph(
        fixture.nodes, fixture.routes).plan;
    solo_rig::TypedVcoBbdFeedbackRuntime runtime;
    REQUIRE(runtime.Prepare(*plan, 48000.0, 1U) ==
        solo_rig::TypedVcoBbdFeedbackRuntimeError::None);
    double output{};
    REQUIRE(runtime.Process(std::numeric_limits<double>::quiet_NaN(), output) ==
        solo_rig::TypedVcoBbdFeedbackRuntimeError::NonFinite);
}
