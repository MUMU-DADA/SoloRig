#include "content/behavior_clocked.hpp"
#include "content/behavior_clocked_runtime.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

solo_rig::BehaviorGraphNodeSpec Node(std::string id, std::string kind, std::string model) {
    return {.id = id, .binding = {.module_id = id, .kind = std::move(kind),
        .model_id = std::move(model)}};
}

std::vector<solo_rig::BehaviorGraphNodeSpec> Nodes() {
    return {Node("bbd", "bbd", "generic-bbd-v1"),
        Node("vco", "vco", "generic-vco-v1")};
}

std::vector<solo_rig::BehaviorPortRouteSpec> Routes() {
    return {{"input", "audio", "bbd", "audio_in"},
        {"vco", "out", "bbd", "clock"},
        {"bbd", "out", "output", "audio"}};
}

} // namespace

TEST_CASE("M18-D3a compiles the explicit VCO to BBD clock chain") {
    const auto result = solo_rig::CompileVcoBbdClockedGraph(Nodes(), Routes());
    REQUIRE(result.error == solo_rig::BehaviorClockedCompileError::None);
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->topological_order.size() == 2U);
    REQUIRE(result.plan->topological_order[0] == "vco");
    REQUIRE(result.plan->topological_order[1] == "bbd");
    const auto repeat = solo_rig::CompileVcoBbdClockedGraph(Nodes(), Routes());
    REQUIRE(repeat.plan->topological_order == result.plan->topological_order);
}

TEST_CASE("M18-D3a rejects missing duplicate fanout and incompatible clock routes") {
    auto routes = Routes();
    routes.erase(routes.begin() + 1);
    REQUIRE(solo_rig::CompileVcoBbdClockedGraph(Nodes(), routes).error ==
        solo_rig::BehaviorClockedCompileError::InvalidPlan);
    routes = Routes();
    routes[1] = routes[0];
    REQUIRE(solo_rig::CompileVcoBbdClockedGraph(Nodes(), routes).error ==
        solo_rig::BehaviorClockedCompileError::DuplicateRoute);
    routes = Routes();
    routes[1].destination_port = "audio_in";
    REQUIRE(solo_rig::CompileVcoBbdClockedGraph(Nodes(), routes).error ==
        solo_rig::BehaviorClockedCompileError::IncompatibleConnection);
    routes = Routes();
    routes[0].destination_port = "clock";
    REQUIRE(solo_rig::CompileVcoBbdClockedGraph(Nodes(), routes).error ==
        solo_rig::BehaviorClockedCompileError::IncompatibleConnection);
    routes = Routes();
    routes[2].source_port = "clock";
    REQUIRE(solo_rig::CompileVcoBbdClockedGraph(Nodes(), routes).error ==
        solo_rig::BehaviorClockedCompileError::UnknownPort);
}

TEST_CASE("M18-D3a rejects unsupported models and atomic invalid plans") {
    auto nodes = Nodes();
    nodes[0].binding.model_id = "blocked.bbd.template";
    const auto result = solo_rig::CompileVcoBbdClockedGraph(nodes, Routes());
    REQUIRE(!result.plan.has_value());
    REQUIRE(result.error == solo_rig::BehaviorClockedCompileError::UnsupportedBlock);
    nodes = Nodes();
    nodes.push_back(Node("extra", "vco", "generic-vco-v1"));
    REQUIRE(solo_rig::CompileVcoBbdClockedGraph(nodes, Routes()).error ==
        solo_rig::BehaviorClockedCompileError::CapacityExceeded);
}

TEST_CASE("M18-D3a runtime is deterministic and uses dynamic VCO clock control") {
    const auto plan = solo_rig::CompileVcoBbdClockedGraph(Nodes(), Routes()).plan;
    solo_rig::TypedVcoBbdClockedRuntime runtime;
    REQUIRE(runtime.Prepare(*plan, 192000.0, 4U) ==
        solo_rig::TypedVcoBbdClockedRuntimeError::None);
    REQUIRE(runtime.IsPrepared());
    REQUIRE(runtime.Size() == 2U);
    double first_sequence[64]{};
    for (double& value : first_sequence) {
        REQUIRE(runtime.Process(0.0, value) ==
            solo_rig::TypedVcoBbdClockedRuntimeError::None);
        REQUIRE(std::isfinite(value));
    }
    REQUIRE(runtime.CurrentClockFrequencyHz() != 100000.0);
    runtime.Reset();
    for (const double expected : first_sequence) {
        double value{};
        REQUIRE(runtime.Process(0.0, value) ==
            solo_rig::TypedVcoBbdClockedRuntimeError::None);
        REQUIRE(value == expected);
    }
    REQUIRE(runtime.SetSampleRate(96000.0) ==
        solo_rig::TypedVcoBbdClockedRuntimeError::None);
    double output{};
    REQUIRE(runtime.Process(std::numeric_limits<double>::quiet_NaN(), output) ==
        solo_rig::TypedVcoBbdClockedRuntimeError::NonFinite);
    static_assert(noexcept(std::declval<solo_rig::TypedVcoBbdClockedRuntime&>().Process(
        0.0, std::declval<double&>())));
}

TEST_CASE("M18-D3a runtime rejects invalid plan, rate, oversampling and nonfinite input") {
    solo_rig::TypedVcoBbdClockedRuntime runtime;
    double output{123.0};
    REQUIRE(runtime.Process(0.0, output) ==
        solo_rig::TypedVcoBbdClockedRuntimeError::NotPrepared);
    REQUIRE(output == 0.0);
    auto plan = solo_rig::CompileVcoBbdClockedGraph(Nodes(), Routes()).plan.value();
    plan.routes[1].destination_port = "audio_in";
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
        solo_rig::TypedVcoBbdClockedRuntimeError::InvalidPlan);
    REQUIRE(!runtime.IsPrepared());
    plan = solo_rig::CompileVcoBbdClockedGraph(Nodes(), Routes()).plan.value();
    REQUIRE(runtime.Prepare(plan, 48000.0, 0U) ==
        solo_rig::TypedVcoBbdClockedRuntimeError::InvalidOversampling);
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) ==
        solo_rig::TypedVcoBbdClockedRuntimeError::None);
    REQUIRE(runtime.SetSampleRate(std::numeric_limits<double>::infinity()) ==
        solo_rig::TypedVcoBbdClockedRuntimeError::InvalidSampleRate);
}
