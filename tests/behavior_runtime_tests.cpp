#include "content/behavior_runtime.hpp"
#include "test_framework.hpp"

#include <limits>
#include <initializer_list>
#include <string>
#include <utility>

namespace {

solo_rig::BehaviorGraphPlan Plan(std::initializer_list<solo_rig::BehaviorKind> kinds) {
    solo_rig::BehaviorGraphPlan plan;
    std::size_t index = 0U;
    for (const auto kind : kinds) {
        const char id = static_cast<char>('a' + index++);
        const std::string name(1, id);
        std::string model = kind == solo_rig::BehaviorKind::Bbd ? "generic-bbd-v1" :
            kind == solo_rig::BehaviorKind::Vco ? "generic-vco-v1" : "generic-spring-v1";
        plan.nodes.push_back({name, {name, kind, model, solo_rig::WhiteboxPlanStatus::Ready}});
    }
    plan.topological_order.reserve(plan.nodes.size());
    for (const auto& node : plan.nodes) plan.topological_order.push_back(node.id);
    plan.routes.push_back({"input", plan.nodes.front().id});
    for (std::size_t route = 1U; route < plan.nodes.size(); ++route) {
        plan.routes.push_back({plan.nodes[route - 1U].id, plan.nodes[route].id});
    }
    plan.routes.push_back({plan.nodes.back().id, "output"});
    return plan;
}

} // namespace

TEST_CASE("M18-D2e2 runtime executes deterministic fixed-capacity single-signal chain") {
    const auto plan = Plan({solo_rig::BehaviorKind::Bbd, solo_rig::BehaviorKind::Vco,
        solo_rig::BehaviorKind::Spring});
    solo_rig::BehaviorGraphRuntime runtime;
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) == solo_rig::BehaviorRuntimeError::None);
    double first{};
    double second{};
    REQUIRE(runtime.Process(0.25, first) == solo_rig::BehaviorRuntimeError::None);
    runtime.Reset();
    REQUIRE(runtime.Process(0.25, second) == solo_rig::BehaviorRuntimeError::None);
    REQUIRE(first == second);
    REQUIRE(runtime.Size() == 3U);
}

TEST_CASE("M18-D2e2 runtime supports reset, oversampling and sample-rate contract") {
    auto plan = Plan({solo_rig::BehaviorKind::Spring});
    solo_rig::BehaviorGraphRuntime runtime;
    REQUIRE(runtime.Prepare(plan, 192000.0, 4U) == solo_rig::BehaviorRuntimeError::None);
    double output{};
    REQUIRE(runtime.Process(0.5, output) == solo_rig::BehaviorRuntimeError::None);
    REQUIRE(runtime.SetSampleRate(96000.0) == solo_rig::BehaviorRuntimeError::None);
    runtime.Reset();
    REQUIRE(runtime.Process(0.0, output) == solo_rig::BehaviorRuntimeError::None);
    REQUIRE(output == 0.0);
}

TEST_CASE("M18-D2e2 runtime rejects nonfinite, unauthorized and incompatible plans") {
    auto plan = Plan({solo_rig::BehaviorKind::Bbd});
    solo_rig::BehaviorGraphRuntime runtime;
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) == solo_rig::BehaviorRuntimeError::None);
    double output{};
    REQUIRE(runtime.Process(std::numeric_limits<double>::quiet_NaN(), output) ==
            solo_rig::BehaviorRuntimeError::NonFinite);
    plan.nodes[0].lowering.model_id = "blocked.bbd.template";
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) == solo_rig::BehaviorRuntimeError::UnsupportedBlock);
    plan = Plan({solo_rig::BehaviorKind::Bbd});
    plan.nodes[0].lowering.kind = solo_rig::BehaviorKind::Vca;
    plan.nodes[0].lowering.model_id = "generic-vca-v1";
    REQUIRE(runtime.Prepare(plan, 48000.0, 1U) == solo_rig::BehaviorRuntimeError::IncompatibleBlock);
}

TEST_CASE("M18-D2e2 runtime enforces fixed capacity and noexcept process boundary") {
    static_assert(noexcept(std::declval<solo_rig::BehaviorGraphRuntime&>().Process(
        0.0, std::declval<double&>())));
    solo_rig::BehaviorGraphPlan oversized;
    for (std::size_t index = 0U; index < solo_rig::kMaximumBehaviorGraphNodes + 1U; ++index) {
        const std::string id = "n" + std::to_string(index);
        oversized.nodes.push_back({id, {id, solo_rig::BehaviorKind::Bbd,
            "generic-bbd-v1", solo_rig::WhiteboxPlanStatus::Ready}});
        oversized.topological_order.push_back(id);
    }
    solo_rig::BehaviorGraphRuntime runtime;
    REQUIRE(runtime.Prepare(oversized, 48000.0, 1U) ==
            solo_rig::BehaviorRuntimeError::CapacityExceeded);
    const auto plan = Plan({solo_rig::BehaviorKind::Spring});
    REQUIRE(runtime.Prepare(plan, 0.0, 1U) == solo_rig::BehaviorRuntimeError::InvalidSampleRate);
    REQUIRE(runtime.Prepare(plan, 48000.0, 17U) ==
            solo_rig::BehaviorRuntimeError::InvalidOversampling);
}

TEST_CASE("M18-D2e2 runtime rejects incomplete or branched route plans atomically") {
    solo_rig::BehaviorGraphRuntime runtime;
    const auto valid = Plan({solo_rig::BehaviorKind::Bbd, solo_rig::BehaviorKind::Spring});
    REQUIRE(runtime.Prepare(valid, 48000.0, 1U) == solo_rig::BehaviorRuntimeError::None);

    auto multiple_inputs = valid;
    multiple_inputs.routes.insert(multiple_inputs.routes.begin() + 1U, {"input", "b"});
    REQUIRE(runtime.Prepare(multiple_inputs, 48000.0, 1U) ==
            solo_rig::BehaviorRuntimeError::InvalidPlan);
    double output{};
    REQUIRE(runtime.Process(0.1, output) == solo_rig::BehaviorRuntimeError::NotPrepared);
    REQUIRE(runtime.Size() == 0U);

    auto isolated_tail = valid;
    isolated_tail.routes = {{"input", "a"}, {"a", "output"}};
    REQUIRE(runtime.Prepare(isolated_tail, 48000.0, 1U) ==
            solo_rig::BehaviorRuntimeError::InvalidPlan);

    auto nonfinal_output = valid;
    nonfinal_output.routes = {{"input", "a"}, {"a", "output"}, {"a", "b"},
        {"b", "output"}};
    REQUIRE(runtime.Prepare(nonfinal_output, 48000.0, 1U) ==
            solo_rig::BehaviorRuntimeError::InvalidPlan);
}
