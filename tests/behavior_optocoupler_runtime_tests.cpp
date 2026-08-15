#include "content/behavior_optocoupler_runtime.hpp"
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

solo_rig::BehaviorTypedGraphPlan OptocouplerPlan() {
    const auto nodes = std::vector<solo_rig::BehaviorGraphNodeSpec>{
        Node("opto", "optocoupler", "generic-optocoupler-v1")};
    const auto routes = std::vector<solo_rig::BehaviorPortRouteSpec>{
        {"input", "emitter", "opto", "emitter"},
        {"opto", "out", "output", "resistance"}};
    const auto result = solo_rig::CompileTypedBehaviorGraph(nodes, routes);
    return *result.plan;
}

} // namespace

TEST_CASE("M18-D2e6 compiles and executes optocoupler resistance output") {
    const auto plan = OptocouplerPlan();
    solo_rig::TypedOptocouplerRuntime runtime;
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedOptocouplerRuntimeError::None);
    double first{};
    double second{};
    REQUIRE(runtime.Process(2.0, first) == solo_rig::TypedOptocouplerRuntimeError::None);
    runtime.Reset();
    REQUIRE(runtime.Process(2.0, second) == solo_rig::TypedOptocouplerRuntimeError::None);
    REQUIRE(first == second);
    REQUIRE(first > 0.0);
    REQUIRE(std::isfinite(first));
    REQUIRE(runtime.Size() == 1U);
}

TEST_CASE("M18-D2e6 optocoupler runtime supports oversampling and sample rate") {
    solo_rig::TypedOptocouplerRuntime runtime;
    REQUIRE(runtime.Prepare(OptocouplerPlan(), 192000.0, 4U) ==
            solo_rig::TypedOptocouplerRuntimeError::None);
    double output{};
    REQUIRE(runtime.Process(5.0, output) == solo_rig::TypedOptocouplerRuntimeError::None);
    REQUIRE(runtime.SetSampleRate(96000.0) ==
            solo_rig::TypedOptocouplerRuntimeError::None);
    runtime.Reset();
    REQUIRE(runtime.Process(0.0, output) == solo_rig::TypedOptocouplerRuntimeError::None);
    REQUIRE(std::isfinite(output));
}

TEST_CASE("M18-D2e6 rejects missing ports, wrong domains and input fanout") {
    auto plan = OptocouplerPlan();
    solo_rig::TypedOptocouplerRuntime runtime;
    plan.routes.erase(plan.routes.begin());
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedOptocouplerRuntimeError::InvalidPlan);
    plan = OptocouplerPlan();
    plan.routes[1].destination_port = "audio";
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedOptocouplerRuntimeError::IncompatibleRoute);
    plan = OptocouplerPlan();
    plan.routes.push_back({"input", "emitter", "opto", "emitter"});
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedOptocouplerRuntimeError::InvalidPlan);
}

TEST_CASE("M18-D2e6 rejects unauthorized, unsupported, nonfinite and invalid rate plans") {
    auto plan = OptocouplerPlan();
    solo_rig::TypedOptocouplerRuntime runtime;
    plan.nodes[0].lowering.model_id = "blocked.optocoupler.template";
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedOptocouplerRuntimeError::UnsupportedBlock);
    plan = OptocouplerPlan();
    plan.nodes[0].lowering.kind = solo_rig::BehaviorKind::Envelope;
    plan.nodes[0].lowering.model_id = "generic-envelope-v1";
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedOptocouplerRuntimeError::UnsupportedBlock);
    plan = OptocouplerPlan();
    REQUIRE(runtime.Prepare(plan, 0.0, 1U) ==
            solo_rig::TypedOptocouplerRuntimeError::InvalidSampleRate);
    REQUIRE(runtime.Prepare(plan, 48000.0, 17U) ==
            solo_rig::TypedOptocouplerRuntimeError::InvalidOversampling);
    REQUIRE(runtime.Prepare(OptocouplerPlan(), 48000.0, 1U) ==
            solo_rig::TypedOptocouplerRuntimeError::None);
    double output{};
    REQUIRE(runtime.Process(std::numeric_limits<double>::quiet_NaN(), output) ==
            solo_rig::TypedOptocouplerRuntimeError::NonFinite);
}
