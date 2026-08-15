#include "content/behavior_typed_runtime.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace {

solo_rig::BehaviorGraphNodeSpec Node(std::string id, std::string kind, std::string model) {
    return {.id = id, .binding = {.module_id = id, .kind = std::move(kind),
        .model_id = std::move(model)}};
}

solo_rig::BehaviorTypedGraphPlan VcaPlan() {
    const auto nodes = std::vector<solo_rig::BehaviorGraphNodeSpec>{
        Node("vca", "vca", "generic-vca-v1"), Node("lfo", "lfo", "generic-lfo-v1")};
    const auto routes = std::vector<solo_rig::BehaviorPortRouteSpec>{
        {"input", "audio", "vca", "audio_in"},
        {"lfo", "out", "vca", "control"}, {"vca", "out", "output", "audio"}};
    const auto compiled = solo_rig::CompileTypedBehaviorGraph(nodes, routes);
    return *compiled.plan;
}

} // namespace

TEST_CASE("M18-D2e4 typed runtime executes LFO control into VCA audio path") {
    const auto plan = VcaPlan();
    solo_rig::TypedBehaviorGraphRuntime runtime;
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedBehaviorRuntimeError::None);
    double first{};
    double second{};
    REQUIRE(runtime.Process(0.25, first) == solo_rig::TypedBehaviorRuntimeError::None);
    runtime.Reset();
    REQUIRE(runtime.Process(0.25, second) == solo_rig::TypedBehaviorRuntimeError::None);
    REQUIRE(first == second);
    REQUIRE(runtime.Size() == 2U);
}

TEST_CASE("M18-D2e4 typed runtime supports sample-rate and oversampling contracts") {
    solo_rig::TypedBehaviorGraphRuntime runtime;
    REQUIRE(runtime.Prepare(VcaPlan(), 192000.0, 4U) ==
            solo_rig::TypedBehaviorRuntimeError::None);
    double output{};
    REQUIRE(runtime.Process(0.5, output) == solo_rig::TypedBehaviorRuntimeError::None);
    REQUIRE(runtime.SetSampleRate(96000.0) == solo_rig::TypedBehaviorRuntimeError::None);
    runtime.Reset();
    REQUIRE(runtime.Process(0.0, output) == solo_rig::TypedBehaviorRuntimeError::None);
    REQUIRE(std::isfinite(output));
}

TEST_CASE("M18-D2e4 typed runtime rejects missing control and implicit constants") {
    auto plan = VcaPlan();
    plan.routes.erase(plan.routes.begin() + 1);
    solo_rig::TypedBehaviorGraphRuntime runtime;
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedBehaviorRuntimeError::InvalidPlan);
    plan = VcaPlan();
    plan.routes[1].source = "input";
    plan.routes[1].source_port = "audio";
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedBehaviorRuntimeError::IncompatibleRoute);
}

TEST_CASE("M18-D2e4 typed runtime rejects wrong domains, unsupported blocks and nonfinite input") {
    auto plan = VcaPlan();
    solo_rig::TypedBehaviorGraphRuntime runtime;
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedBehaviorRuntimeError::None);
    double output{};
    REQUIRE(runtime.Process(std::numeric_limits<double>::quiet_NaN(), output) ==
            solo_rig::TypedBehaviorRuntimeError::NonFinite);
    plan = VcaPlan();
    plan.nodes[0].lowering.kind = solo_rig::BehaviorKind::Envelope;
    plan.nodes[0].lowering.model_id = "generic-envelope-v1";
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedBehaviorRuntimeError::UnsupportedBlock);
    plan = VcaPlan();
    plan.nodes[1].lowering.model_id = "blocked.lfo.template";
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedBehaviorRuntimeError::UnsupportedBlock);
}

TEST_CASE("M18-D2e4 typed runtime rejects malformed routes atomically") {
    auto plan = VcaPlan();
    solo_rig::TypedBehaviorGraphRuntime runtime;
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedBehaviorRuntimeError::None);
    plan.routes[2].source = "lfo";
    plan.routes[2].source_port = "out";
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
            solo_rig::TypedBehaviorRuntimeError::IncompatibleRoute);
    double output{};
    REQUIRE(runtime.Process(0.1, output) == solo_rig::TypedBehaviorRuntimeError::NotPrepared);
    REQUIRE(runtime.Size() == 0U);
    plan = VcaPlan();
    REQUIRE(runtime.Prepare(plan, 0.0, 1U) ==
            solo_rig::TypedBehaviorRuntimeError::InvalidSampleRate);
    REQUIRE(runtime.Prepare(plan, 48000.0, 17U) ==
            solo_rig::TypedBehaviorRuntimeError::InvalidOversampling);
}
