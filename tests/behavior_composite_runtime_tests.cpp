#include "content/behavior_composite_runtime.hpp"
#include "content/behavior_composite.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

solo_rig::BehaviorGraphNodeSpec Node(std::string id, std::string kind, std::string model) {
    return {.id = id, .binding = {.module_id = id, .kind = std::move(kind),
        .model_id = std::move(model)}};
}

solo_rig::BehaviorTypedGraphPlan Plan() {
    const auto nodes = std::vector<solo_rig::BehaviorGraphNodeSpec>{
        Node("vca", "vca", "generic-vca-v1"),
        Node("envelope", "envelope", "generic-envelope-v1")};
    const auto routes = std::vector<solo_rig::BehaviorPortRouteSpec>{
        {"input", "audio", "vca", "audio_in"},
        {"input", "detector", "envelope", "detector"},
        {"envelope", "out", "vca", "control"},
        {"vca", "out", "output", "audio"}};
    const auto result = solo_rig::CompileEnvelopeVcaComposite(nodes, routes);
    return *result.plan;
}

} // namespace

TEST_CASE("M18-D2e7 composite runtime executes detector-controlled VCA deterministically") {
    const auto plan = Plan();
    solo_rig::TypedEnvelopeVcaCompositeRuntime runtime;
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedEnvelopeVcaRuntimeError::None);
    double first{};
    double second{};
    REQUIRE(runtime.Process(0.5, 0.75, first) ==
            solo_rig::TypedEnvelopeVcaRuntimeError::None);
    runtime.Reset();
    REQUIRE(runtime.Process(0.5, 0.75, second) ==
            solo_rig::TypedEnvelopeVcaRuntimeError::None);
    REQUIRE(first == second);
    REQUIRE(std::isfinite(first));
    REQUIRE(runtime.Size() == 2U);
}

TEST_CASE("M18-D2e7 composite runtime supports reset sample rate and oversampling") {
    solo_rig::TypedEnvelopeVcaCompositeRuntime runtime;
    REQUIRE(runtime.Prepare(Plan(), 192000.0, 4U) ==
            solo_rig::TypedEnvelopeVcaRuntimeError::None);
    double output{};
    REQUIRE(runtime.Process(0.5, 1.0, output) ==
            solo_rig::TypedEnvelopeVcaRuntimeError::None);
    REQUIRE(runtime.SetSampleRate(96000.0) ==
            solo_rig::TypedEnvelopeVcaRuntimeError::None);
    runtime.Reset();
    REQUIRE(runtime.Process(0.0, 0.0, output) ==
            solo_rig::TypedEnvelopeVcaRuntimeError::None);
    REQUIRE(std::isfinite(output));
    static_assert(noexcept(std::declval<solo_rig::TypedEnvelopeVcaCompositeRuntime&>().Process(
        0.0, 0.0, std::declval<double&>())));
}

TEST_CASE("M18-D2e7 composite runtime rejects fanout wrong ports and unsupported blocks") {
    auto plan = Plan();
    solo_rig::TypedEnvelopeVcaCompositeRuntime runtime;
    plan.routes[1] = {"input", "detector", "vca", "control"};
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedEnvelopeVcaRuntimeError::IncompatibleRoute);
    REQUIRE(!runtime.IsPrepared());
    REQUIRE(runtime.Size() == 0U);
    plan = Plan();
    plan.routes[0].destination_port = "control";
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedEnvelopeVcaRuntimeError::IncompatibleRoute);
    plan = Plan();
    plan.nodes[0].lowering.model_id = "blocked.envelope.template";
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedEnvelopeVcaRuntimeError::UnsupportedBlock);
}

TEST_CASE("M18-D2e7 composite runtime rejects nonfinite input and invalid rates atomically") {
    solo_rig::TypedEnvelopeVcaCompositeRuntime runtime;
    REQUIRE(runtime.Prepare(Plan(), 48000.0, 1U) ==
            solo_rig::TypedEnvelopeVcaRuntimeError::None);
    double output{};
    REQUIRE(runtime.Process(std::numeric_limits<double>::quiet_NaN(), 0.0, output) ==
            solo_rig::TypedEnvelopeVcaRuntimeError::NonFinite);
    REQUIRE(runtime.Process(0.0, std::numeric_limits<double>::infinity(), output) ==
            solo_rig::TypedEnvelopeVcaRuntimeError::NonFinite);
    auto malformed = Plan();
    malformed.routes.pop_back();
    REQUIRE(runtime.Prepare(malformed, 48000.0, 1U) ==
            solo_rig::TypedEnvelopeVcaRuntimeError::InvalidPlan);
    REQUIRE(!runtime.IsPrepared());
    REQUIRE(runtime.Prepare(Plan(), 0.0, 1U) ==
            solo_rig::TypedEnvelopeVcaRuntimeError::InvalidSampleRate);
    REQUIRE(runtime.Prepare(Plan(), 48000.0, 17U) ==
            solo_rig::TypedEnvelopeVcaRuntimeError::InvalidOversampling);
    REQUIRE(runtime.Process(0.0, 0.0, output) ==
            solo_rig::TypedEnvelopeVcaRuntimeError::NotPrepared);
}
