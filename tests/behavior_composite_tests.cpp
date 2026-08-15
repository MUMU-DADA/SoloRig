#include "content/behavior_composite.hpp"
#include "test_framework.hpp"

#include <string>
#include <utility>
#include <vector>

namespace {

solo_rig::BehaviorGraphNodeSpec Node(std::string id, std::string kind, std::string model) {
    return {.id = id, .binding = {.module_id = id, .kind = std::move(kind),
        .model_id = std::move(model)}};
}

std::vector<solo_rig::BehaviorGraphNodeSpec> Nodes() {
    return {Node("vca", "vca", "generic-vca-v1"),
        Node("envelope", "envelope", "generic-envelope-v1")};
}

std::vector<solo_rig::BehaviorPortRouteSpec> Routes() {
    return {{"input", "audio", "vca", "audio_in"},
        {"input", "detector", "envelope", "detector"},
        {"envelope", "out", "vca", "control"},
        {"vca", "out", "output", "audio"}};
}

} // namespace

TEST_CASE("M18-D2e7 compiles the explicit envelope to VCA composite") {
    const auto result = solo_rig::CompileEnvelopeVcaComposite(Nodes(), Routes());
    REQUIRE(result.error == solo_rig::BehaviorPortCompileError::None);
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->nodes.size() == 2U);
    REQUIRE(result.plan->nodes[0].id == "envelope");
    REQUIRE(result.plan->nodes[1].id == "vca");
    REQUIRE(result.plan->topological_order[0] == "envelope");
    REQUIRE(result.plan->topological_order[1] == "vca");
    const auto repeat = solo_rig::CompileEnvelopeVcaComposite(Nodes(), Routes());
    REQUIRE(repeat.plan->topological_order == result.plan->topological_order);
}

TEST_CASE("M18-D2e7 rejects missing duplicate and fanout external routes") {
    auto routes = Routes();
    routes.erase(routes.begin() + 1);
    REQUIRE(solo_rig::CompileEnvelopeVcaComposite(Nodes(), routes).error ==
            solo_rig::BehaviorPortCompileError::MissingPort);
    routes = Routes();
    routes.push_back(routes.front());
    REQUIRE(solo_rig::CompileEnvelopeVcaComposite(Nodes(), routes).error ==
            solo_rig::BehaviorPortCompileError::DuplicateRoute);
    routes = Routes();
    routes.push_back({"input", "detector", "vca", "control"});
    REQUIRE(solo_rig::CompileEnvelopeVcaComposite(Nodes(), routes).error ==
            solo_rig::BehaviorPortCompileError::FanOutUnsupported);
}

TEST_CASE("M18-D2e7 rejects implicit controls wrong domains and unsupported topology") {
    auto routes = Routes();
    routes[2] = {"input", "control", "vca", "control"};
    REQUIRE(solo_rig::CompileEnvelopeVcaComposite(Nodes(), routes).error ==
            solo_rig::BehaviorPortCompileError::IncompatibleConnection);
    routes = Routes();
    routes[0].destination_port = "control";
    REQUIRE(solo_rig::CompileEnvelopeVcaComposite(Nodes(), routes).error ==
            solo_rig::BehaviorPortCompileError::IncompatibleConnection);
    auto nodes = Nodes();
    nodes[0].binding.model_id = "blocked.vca.template";
    REQUIRE(solo_rig::CompileEnvelopeVcaComposite(nodes, Routes()).plan.has_value() == false);
    nodes = Nodes();
    nodes.push_back(Node("extra", "vca", "generic-vca-v1"));
    REQUIRE(solo_rig::CompileEnvelopeVcaComposite(nodes, Routes()).error ==
            solo_rig::BehaviorPortCompileError::CapacityExceeded);
    routes = Routes();
    routes[2] = {"vca", "out", "envelope", "detector"};
    REQUIRE(solo_rig::CompileEnvelopeVcaComposite(Nodes(), routes).error ==
            solo_rig::BehaviorPortCompileError::IncompatibleConnection);
}
