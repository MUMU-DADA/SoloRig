#include "content/behavior_graph.hpp"
#include "test_framework.hpp"

#include <string>
#include <vector>

namespace {

solo_rig::BehaviorGraphNodeSpec Node(std::string id, std::string kind, std::string model) {
    return {.id = id, .binding = {.module_id = id, .kind = kind, .model_id = model}};
}

std::vector<solo_rig::BehaviorGraphNodeSpec> AuthorizedNodes() {
    return {
        Node("bbd", "bbd", "generic-bbd-v1"),
        Node("vco", "vco", "generic-vco-v1"),
        Node("spring", "spring", "generic-spring-v1"),
    };
}

} // namespace

TEST_CASE("M18-D2e1 behavior graph lowers authorized nodes in deterministic route order") {
    const std::vector<solo_rig::BehaviorGraphNodeSpec> nodes{
        Node("second", "spring", "generic-spring-v1"),
        Node("first", "vco", "generic-vco-v1"),
    };
    const std::vector<solo_rig::BehaviorGraphRouteSpec> routes{
        {"input", "first"}, {"first", "second"}, {"second", "output"}};
    const auto result = solo_rig::CompileBehaviorGraph(nodes, routes);
    REQUIRE(result.error == solo_rig::BehaviorGraphError::None);
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->nodes.size() == 2U);
    REQUIRE(result.plan->nodes[0].id == "first");
    REQUIRE(result.plan->nodes[1].id == "second");
    REQUIRE(result.plan->routes.size() == 3U);
    REQUIRE(result.plan->routes[0].source == "input");
    REQUIRE(result.plan->routes[2].destination == "output");
    REQUIRE(result.plan->nodes[0].lowering.kind == solo_rig::BehaviorKind::Vco);
    REQUIRE(result.plan->nodes[1].lowering.kind == solo_rig::BehaviorKind::Spring);
}

TEST_CASE("M18-D2e1 behavior graph accepts single-signal behavior families") {
    const auto nodes = std::vector<solo_rig::BehaviorGraphNodeSpec>{
        Node("bbd", "bbd", "generic-bbd-v1"),
        Node("vco", "vco", "generic-vco-v1"),
        Node("spring", "spring", "generic-spring-v1")};
    std::vector<solo_rig::BehaviorGraphRouteSpec> routes;
    routes.push_back({"input", nodes.front().id});
    for (std::size_t index = 1U; index < nodes.size(); ++index) {
        routes.push_back({nodes[index - 1U].id, nodes[index].id});
    }
    routes.push_back({nodes.back().id, "output"});
    const auto result = solo_rig::CompileBehaviorGraph(nodes, routes);
    REQUIRE(result.error == solo_rig::BehaviorGraphError::None);
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->nodes.size() == nodes.size());
    REQUIRE(result.plan->topological_order.size() == nodes.size());
}

TEST_CASE("M18-D2e2 compile rejects behavior blocks without a single-signal contract") {
    const auto nodes = std::vector<solo_rig::BehaviorGraphNodeSpec>{
        Node("vca", "vca", "generic-vca-v1"),
        Node("envelope", "envelope", "generic-envelope-v1"),
        Node("lfo", "lfo", "generic-lfo-v1"),
        Node("optocoupler", "optocoupler", "generic-optocoupler-v1")};
    for (const auto& node : nodes) {
        REQUIRE(solo_rig::CompileBehaviorGraph({node}, {{"input", node.id}, {node.id, "output"}}).error ==
                solo_rig::BehaviorGraphError::IncompatibleBlock);
    }
}

TEST_CASE("M18-D2e1 behavior graph rejects duplicate, unknown and malformed endpoints") {
    auto nodes = AuthorizedNodes();
    auto routes = std::vector<solo_rig::BehaviorGraphRouteSpec>{{"input", "bbd"},
        {"input", "bbd"}, {"bbd", "output"}};
    REQUIRE(CompileBehaviorGraph(nodes, routes).error ==
            solo_rig::BehaviorGraphError::DuplicateRoute);
    routes = {{"unknown", "bbd"}, {"bbd", "output"}};
    REQUIRE(CompileBehaviorGraph(nodes, routes).error ==
            solo_rig::BehaviorGraphError::UnknownEndpoint);
    routes = {{"input", ""}, {"bbd", "output"}};
    REQUIRE(CompileBehaviorGraph(nodes, routes).error ==
            solo_rig::BehaviorGraphError::InvalidRoute);
    nodes.push_back(Node("bbd", "bbd", "generic-bbd-v1"));
    REQUIRE(CompileBehaviorGraph(nodes, {{"input", "bbd"}, {"bbd", "output"}}).error ==
            solo_rig::BehaviorGraphError::DuplicateNode);
}

TEST_CASE("M18-D2e1 behavior graph rejects missing ports, fan-in, fan-out and cycles") {
    const auto nodes = std::vector<solo_rig::BehaviorGraphNodeSpec>{
        Node("a", "vco", "generic-vco-v1"), Node("b", "spring", "generic-spring-v1"),
        Node("c", "bbd", "generic-bbd-v1")};
    REQUIRE(CompileBehaviorGraph(nodes, {}).error ==
            solo_rig::BehaviorGraphError::MissingOutputRoute);
    REQUIRE(CompileBehaviorGraph(nodes, {{"input", "a"}, {"b", "a"}, {"a", "output"}}).error ==
            solo_rig::BehaviorGraphError::MultipleSignalInputs);
    REQUIRE(CompileBehaviorGraph(nodes, {{"input", "a"}, {"a", "b"}, {"a", "c"},
        {"b", "output"}}).error == solo_rig::BehaviorGraphError::FanOutUnsupported);
    REQUIRE(CompileBehaviorGraph(nodes, {{"a", "b"}, {"b", "a"}}).error ==
            solo_rig::BehaviorGraphError::CycleDetected);
    REQUIRE(CompileBehaviorGraph(nodes, {{"input", "a"}, {"a", "output"}}).error ==
            solo_rig::BehaviorGraphError::MissingInputRoute);
}

TEST_CASE("M18-D2e1 behavior graph rejects blocked nodes and capacity overflow atomically") {
    const std::vector<solo_rig::BehaviorGraphNodeSpec> blocked{
        Node("blocked", "spring", "blocked.spring.template")};
    REQUIRE(CompileBehaviorGraph(blocked, {{"input", "blocked"}, {"blocked", "output"}}).error ==
            solo_rig::BehaviorGraphError::BlockedNode);

    std::vector<solo_rig::BehaviorGraphNodeSpec> too_many;
    std::vector<solo_rig::BehaviorGraphRouteSpec> routes;
    for (int index = 0; index < 17; ++index) {
        too_many.push_back(Node("n" + std::to_string(index), "vco", "generic-vco-v1"));
        routes.push_back({index == 0 ? "input" : "n" + std::to_string(index - 1),
            "n" + std::to_string(index)});
    }
    routes.push_back({"n16", "output"});
    REQUIRE(CompileBehaviorGraph(too_many, routes).error ==
            solo_rig::BehaviorGraphError::CapacityExceeded);
}

TEST_CASE("M18-D2e1 behavior graph rejects nonfinite and unsupported port semantics") {
    const auto nodes = std::vector<solo_rig::BehaviorGraphNodeSpec>{
        Node("a", "vco", "generic-vco-v1"), Node("b", "spring", "generic-spring-v1")};
    REQUIRE(CompileBehaviorGraph(nodes, {{"input", "a"}, {"a", "input"},
        {"a", "output"}}).error == solo_rig::BehaviorGraphError::InvalidRoute);
    REQUIRE(CompileBehaviorGraph(nodes, {{"input", "a"}, {"a", "b"},
        {"b", "output"}, {"output", "b"}}).error ==
            solo_rig::BehaviorGraphError::InvalidRoute);
}
