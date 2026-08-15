#include "m19a1_validation_harness.hpp"

#include "../fixtures/whitebox/m19a1_behavior_chain_fixtures.hpp"
#include "content/behavior_composite.hpp"
#include "content/behavior_composite_runtime.hpp"
#include "content/behavior_envelope_runtime.hpp"
#include "content/behavior_optocoupler_runtime.hpp"
#include "content/behavior_runtime.hpp"
#include "content/behavior_typed_runtime.hpp"
#include "test_framework.hpp"

#include <limits>
#include <utility>

namespace {

void RequireComplete(const solo_rig::m19a1_validation::ChainEvidence& evidence) {
    REQUIRE(evidence.compiled);
    REQUIRE(evidence.prepared);
    REQUIRE(evidence.finite_output);
    REQUIRE(evidence.reset_deterministic);
    REQUIRE(evidence.sample_rate_and_oversampling);
}

} // namespace

TEST_CASE("M19-A1 validates representative generic tremolo and optical-like chains") {
    RequireComplete(solo_rig::m19a1_validation::ValidateTremolo());
    RequireComplete(solo_rig::m19a1_validation::ValidateEnvelopeVcaOpticalLike());
}

TEST_CASE("M19-A1 validates standalone detector optocoupler and single-signal chains") {
    RequireComplete(solo_rig::m19a1_validation::ValidateEnvelopeDetector());
    RequireComplete(solo_rig::m19a1_validation::ValidateOptocouplerResistance());
    RequireComplete(solo_rig::m19a1_validation::ValidateBbdVcoSpring());
}

TEST_CASE("M19-A1 rejects wrong domains and implicit route substitutions") {
    auto tremolo = solo_rig::m19a1_fixtures::TremoloLfoToVca();
    tremolo.routes[1].source_port = "audio";
    REQUIRE(solo_rig::CompileTypedBehaviorGraph(tremolo.nodes, tremolo.routes).error ==
            solo_rig::BehaviorPortCompileError::UnknownPort);

    auto optical = solo_rig::m19a1_fixtures::EnvelopeVcaOpticalLike();
    optical.routes[0].destination_port = "control";
    REQUIRE(solo_rig::CompileEnvelopeVcaComposite(optical.nodes, optical.routes).error ==
            solo_rig::BehaviorPortCompileError::IncompatibleConnection);

    auto detector = solo_rig::m19a1_fixtures::EnvelopeDetectorOutput();
    detector.routes[1].destination_port = "audio";
    REQUIRE(solo_rig::CompileTypedBehaviorGraph(detector.nodes, detector.routes).error ==
            solo_rig::BehaviorPortCompileError::IncompatibleConnection);

    auto optocoupler = solo_rig::m19a1_fixtures::OptocouplerResistance();
    optocoupler.routes[1].destination_port = "audio";
    REQUIRE(solo_rig::CompileTypedBehaviorGraph(optocoupler.nodes, optocoupler.routes).error ==
            solo_rig::BehaviorPortCompileError::IncompatibleConnection);

    auto serial = solo_rig::m19a1_fixtures::BbdVcoSpringSerial();
    serial.routes.pop_back();
    REQUIRE(solo_rig::CompileBehaviorGraph(serial.nodes, serial.routes).error ==
            solo_rig::BehaviorGraphError::MissingOutputRoute);
}

TEST_CASE("M19-A1 rejects nonfinite inputs on every representative runtime") {
    const auto tremolo_fixture = solo_rig::m19a1_fixtures::TremoloLfoToVca();
    const auto tremolo_plan = solo_rig::CompileTypedBehaviorGraph(
        tremolo_fixture.nodes, tremolo_fixture.routes).plan;
    solo_rig::TypedBehaviorGraphRuntime tremolo_runtime;
    REQUIRE(tremolo_runtime.Prepare(*tremolo_plan, 48000.0, 1U) ==
            solo_rig::TypedBehaviorRuntimeError::None);
    double output{};
    REQUIRE(tremolo_runtime.Process(std::numeric_limits<double>::quiet_NaN(), output) ==
            solo_rig::TypedBehaviorRuntimeError::NonFinite);

    const auto optical_fixture = solo_rig::m19a1_fixtures::EnvelopeVcaOpticalLike();
    const auto optical_plan = solo_rig::CompileEnvelopeVcaComposite(
        optical_fixture.nodes, optical_fixture.routes).plan;
    solo_rig::TypedEnvelopeVcaCompositeRuntime optical_runtime;
    REQUIRE(optical_runtime.Prepare(*optical_plan, 48000.0, 1U) ==
            solo_rig::TypedEnvelopeVcaRuntimeError::None);
    REQUIRE(optical_runtime.Process(std::numeric_limits<double>::infinity(), 0.0, output) ==
            solo_rig::TypedEnvelopeVcaRuntimeError::NonFinite);
    REQUIRE(optical_runtime.Process(0.0, std::numeric_limits<double>::quiet_NaN(), output) ==
            solo_rig::TypedEnvelopeVcaRuntimeError::NonFinite);

    const auto detector_fixture = solo_rig::m19a1_fixtures::EnvelopeDetectorOutput();
    const auto detector_plan = solo_rig::CompileTypedBehaviorGraph(
        detector_fixture.nodes, detector_fixture.routes).plan;
    solo_rig::TypedEnvelopeRuntime detector_runtime;
    REQUIRE(detector_runtime.Prepare(*detector_plan, 48000.0, 1U) ==
            solo_rig::TypedEnvelopeRuntimeError::None);
    REQUIRE(detector_runtime.Process(std::numeric_limits<double>::infinity(), output) ==
            solo_rig::TypedEnvelopeRuntimeError::NonFinite);

    const auto optocoupler_fixture = solo_rig::m19a1_fixtures::OptocouplerResistance();
    const auto optocoupler_plan = solo_rig::CompileTypedBehaviorGraph(
        optocoupler_fixture.nodes, optocoupler_fixture.routes).plan;
    solo_rig::TypedOptocouplerRuntime optocoupler_runtime;
    REQUIRE(optocoupler_runtime.Prepare(*optocoupler_plan, 48000.0, 1U) ==
            solo_rig::TypedOptocouplerRuntimeError::None);
    REQUIRE(optocoupler_runtime.Process(std::numeric_limits<double>::quiet_NaN(), output) ==
            solo_rig::TypedOptocouplerRuntimeError::NonFinite);

    const auto serial_fixture = solo_rig::m19a1_fixtures::BbdVcoSpringSerial();
    const auto serial_plan = solo_rig::CompileBehaviorGraph(
        serial_fixture.nodes, serial_fixture.routes).plan;
    solo_rig::BehaviorGraphRuntime serial_runtime;
    REQUIRE(serial_runtime.Prepare(*serial_plan, 48000.0, 1U) ==
            solo_rig::BehaviorRuntimeError::None);
    REQUIRE(serial_runtime.Process(std::numeric_limits<double>::quiet_NaN(), output) ==
            solo_rig::BehaviorRuntimeError::NonFinite);
}

TEST_CASE("M19-A1 representative runtimes expose fixed no-throw process boundaries") {
    static_assert(noexcept(std::declval<solo_rig::TypedBehaviorGraphRuntime&>().Process(
        0.0, std::declval<double&>())));
    static_assert(noexcept(std::declval<solo_rig::TypedEnvelopeVcaCompositeRuntime&>().Process(
        0.0, 0.0, std::declval<double&>())));
    static_assert(noexcept(std::declval<solo_rig::TypedEnvelopeRuntime&>().Process(
        0.0, std::declval<double&>())));
    static_assert(noexcept(std::declval<solo_rig::TypedOptocouplerRuntime&>().Process(
        0.0, std::declval<double&>())));
    static_assert(noexcept(std::declval<solo_rig::BehaviorGraphRuntime&>().Process(
        0.0, std::declval<double&>())));
    REQUIRE(true);
}
