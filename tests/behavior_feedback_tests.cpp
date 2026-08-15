#include "content/behavior_feedback.hpp"
#include "content/behavior_feedback_runtime.hpp"
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
        {"bbd", "out", "bbd", "feedback"},
        {"bbd", "out", "output", "audio"}};
}

} // namespace

TEST_CASE("M18-D3b compiles exactly one delayed feedback edge") {
    const auto result = solo_rig::CompileVcoBbdFeedbackGraph(Nodes(), Routes());
    REQUIRE(result.error == solo_rig::BehaviorFeedbackCompileError::None);
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->topological_order[0] == "vco");
    REQUIRE(result.plan->topological_order[1] == "bbd");
    const auto repeat = solo_rig::CompileVcoBbdFeedbackGraph(Nodes(), Routes());
    REQUIRE(repeat.plan->topological_order == result.plan->topological_order);
}

TEST_CASE("M18-D3b rejects missing duplicate multiple and zero-delay feedback routes") {
    auto routes = Routes();
    routes.erase(routes.begin() + 2);
    REQUIRE(solo_rig::CompileVcoBbdFeedbackGraph(Nodes(), routes).error ==
        solo_rig::BehaviorFeedbackCompileError::InvalidPlan);
    routes = Routes();
    routes[3] = routes[2];
    REQUIRE(solo_rig::CompileVcoBbdFeedbackGraph(Nodes(), routes).error ==
        solo_rig::BehaviorFeedbackCompileError::DuplicateRoute);
    routes = Routes();
    routes[2].destination_port = "audio_in";
    REQUIRE(solo_rig::CompileVcoBbdFeedbackGraph(Nodes(), routes).error ==
        solo_rig::BehaviorFeedbackCompileError::ZeroDelayCycle);
    routes = Routes();
    routes[2] = {"bbd", "out", "vco", "out"};
    REQUIRE(solo_rig::CompileVcoBbdFeedbackGraph(Nodes(), routes).error ==
        solo_rig::BehaviorFeedbackCompileError::UnknownPort);
}

TEST_CASE("M18-D3b rejects wrong feedback domains and unsupported graph shapes") {
    auto routes = Routes();
    routes[2].destination_port = "audio_in";
    REQUIRE(solo_rig::CompileVcoBbdFeedbackGraph(Nodes(), routes).error ==
        solo_rig::BehaviorFeedbackCompileError::ZeroDelayCycle);
    routes = Routes();
    routes[0].destination_port = "clock";
    REQUIRE(solo_rig::CompileVcoBbdFeedbackGraph(Nodes(), routes).error ==
        solo_rig::BehaviorFeedbackCompileError::IncompatibleConnection);
    routes = Routes();
    routes[2].source_port = "feedback";
    REQUIRE(solo_rig::CompileVcoBbdFeedbackGraph(Nodes(), routes).error ==
        solo_rig::BehaviorFeedbackCompileError::UnknownPort);
    auto nodes = Nodes();
    nodes[0].binding.model_id = "blocked.bbd.template";
    const auto blocked = solo_rig::CompileVcoBbdFeedbackGraph(nodes, Routes());
    REQUIRE(!blocked.plan.has_value());
    REQUIRE(blocked.error == solo_rig::BehaviorFeedbackCompileError::UnsupportedBlock);
}

TEST_CASE("M18-D3b feedback runtime is deterministic and cleans failed Prepare") {
    const auto plan = solo_rig::CompileVcoBbdFeedbackGraph(Nodes(), Routes()).plan;
    solo_rig::TypedVcoBbdFeedbackRuntime runtime;
    REQUIRE(runtime.Prepare(*plan, 192000.0, 4U) ==
        solo_rig::TypedVcoBbdFeedbackRuntimeError::None);
    double first[96]{};
    for (double& value : first) {
        REQUIRE(runtime.Process(0.0, value) ==
            solo_rig::TypedVcoBbdFeedbackRuntimeError::None);
        REQUIRE(std::isfinite(value));
    }
    REQUIRE(runtime.PreviousFeedbackOutput() == 0.0);
    runtime.Reset();
    for (const double expected : first) {
        double value{};
        REQUIRE(runtime.Process(0.0, value) ==
            solo_rig::TypedVcoBbdFeedbackRuntimeError::None);
        REQUIRE(value == expected);
    }
    auto invalid = *plan;
    invalid.routes[2].destination_port = "clock";
    REQUIRE(runtime.Prepare(invalid, 48000.0, 1U) ==
        solo_rig::TypedVcoBbdFeedbackRuntimeError::InvalidPlan);
    REQUIRE(!runtime.IsPrepared());
    double output{123.0};
    REQUIRE(runtime.Process(0.0, output) ==
        solo_rig::TypedVcoBbdFeedbackRuntimeError::NotPrepared);
    REQUIRE(output == 0.0);
    REQUIRE(runtime.Prepare(*plan, 48000.0, 1U) ==
        solo_rig::TypedVcoBbdFeedbackRuntimeError::None);
    REQUIRE(runtime.SetSampleRate(std::numeric_limits<double>::quiet_NaN()) ==
        solo_rig::TypedVcoBbdFeedbackRuntimeError::InvalidSampleRate);
    REQUIRE(runtime.Process(std::numeric_limits<double>::infinity(), output) ==
        solo_rig::TypedVcoBbdFeedbackRuntimeError::NonFinite);
    static_assert(noexcept(std::declval<solo_rig::TypedVcoBbdFeedbackRuntime&>().Process(
        0.0, std::declval<double&>())));
}
