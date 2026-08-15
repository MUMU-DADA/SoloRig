#include "content/behavior_envelope_runtime.hpp"
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

solo_rig::BehaviorTypedGraphPlan EnvelopePlan() {
    const auto nodes = std::vector<solo_rig::BehaviorGraphNodeSpec>{
        Node("envelope", "envelope", "generic-envelope-v1")};
    const auto routes = std::vector<solo_rig::BehaviorPortRouteSpec>{
        {"input", "audio", "envelope", "detector"},
        {"envelope", "out", "output", "control"}};
    const auto result = solo_rig::CompileTypedBehaviorGraph(nodes, routes);
    return *result.plan;
}

} // namespace

TEST_CASE("M18-D2e5 compiles and executes envelope detector control output") {
    const auto plan = EnvelopePlan();
    solo_rig::TypedEnvelopeRuntime runtime;
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedEnvelopeRuntimeError::None);
    double first{};
    double second{};
    REQUIRE(runtime.Process(0.5, first) == solo_rig::TypedEnvelopeRuntimeError::None);
    runtime.Reset();
    REQUIRE(runtime.Process(0.5, second) == solo_rig::TypedEnvelopeRuntimeError::None);
    REQUIRE(first == second);
    REQUIRE(std::isfinite(first));
    REQUIRE(runtime.Size() == 1U);
}

TEST_CASE("M18-D2e5 envelope runtime supports oversampling and sample rate") {
    solo_rig::TypedEnvelopeRuntime runtime;
    REQUIRE(runtime.Prepare(EnvelopePlan(), 192000.0, 4U) ==
            solo_rig::TypedEnvelopeRuntimeError::None);
    double output{};
    REQUIRE(runtime.Process(1.0, output) == solo_rig::TypedEnvelopeRuntimeError::None);
    REQUIRE(runtime.SetSampleRate(96000.0) == solo_rig::TypedEnvelopeRuntimeError::None);
    runtime.Reset();
    REQUIRE(runtime.Process(0.0, output) == solo_rig::TypedEnvelopeRuntimeError::None);
    REQUIRE(output == 0.0);
}

TEST_CASE("M18-D2e5 rejects missing ports, wrong output domain and input fanout") {
    auto plan = EnvelopePlan();
    solo_rig::TypedEnvelopeRuntime runtime;
    plan.routes.erase(plan.routes.begin());
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedEnvelopeRuntimeError::InvalidPlan);
    plan = EnvelopePlan();
    plan.routes[1].destination_port = "audio";
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedEnvelopeRuntimeError::IncompatibleRoute);
    plan = EnvelopePlan();
    plan.routes.push_back({"input", "audio", "envelope", "detector"});
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedEnvelopeRuntimeError::InvalidPlan);
}

TEST_CASE("M18-D2e5 rejects unauthorized, unsupported, nonfinite and invalid rate plans") {
    auto plan = EnvelopePlan();
    solo_rig::TypedEnvelopeRuntime runtime;
    plan.nodes[0].lowering.model_id = "blocked.envelope.template";
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedEnvelopeRuntimeError::UnsupportedBlock);
    plan = EnvelopePlan();
    plan.nodes[0].lowering.kind = solo_rig::BehaviorKind::Optocoupler;
    plan.nodes[0].lowering.model_id = "generic-optocoupler-v1";
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedEnvelopeRuntimeError::UnsupportedBlock);
    plan = EnvelopePlan();
    REQUIRE(runtime.Prepare(plan, 0.0, 1U) ==
            solo_rig::TypedEnvelopeRuntimeError::InvalidSampleRate);
    REQUIRE(runtime.Prepare(plan, 48000.0, 17U) ==
            solo_rig::TypedEnvelopeRuntimeError::InvalidOversampling);
    REQUIRE(runtime.Prepare(EnvelopePlan(), 48000.0, 1U) ==
            solo_rig::TypedEnvelopeRuntimeError::None);
    double output{};
    REQUIRE(runtime.Process(std::numeric_limits<double>::quiet_NaN(), output) ==
            solo_rig::TypedEnvelopeRuntimeError::NonFinite);
}
