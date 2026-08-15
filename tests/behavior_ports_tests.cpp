#include "content/behavior_ports.hpp"
#include "test_framework.hpp"

#include <string>
#include <utility>
#include <vector>

namespace {

solo_rig::BehaviorGraphNodeSpec Node(std::string id, std::string kind, std::string model) {
    return {.id = id, .binding = {.module_id = id, .kind = std::move(kind),
        .model_id = std::move(model)}};
}

std::vector<solo_rig::BehaviorGraphNodeSpec> VcaWithLfo() {
    return {Node("vca", "vca", "generic-vca-v1"),
        Node("lfo", "lfo", "generic-lfo-v1")};
}

std::vector<solo_rig::BehaviorPortRouteSpec> VcaWithLfoRoutes() {
    return {{"input", "audio", "vca", "audio_in"},
        {"lfo", "out", "vca", "control"}, {"vca", "out", "output", "audio"}};
}

std::vector<solo_rig::BehaviorGraphNodeSpec> VcaWithTwoLfo() {
    return {Node("vca", "vca", "generic-vca-v1"),
        Node("lfo", "lfo", "generic-lfo-v1"), Node("lfo2", "lfo", "generic-lfo-v1")};
}

} // namespace

TEST_CASE("M18-D2e3 exposes typed contracts for all authorized behavior families") {
    const auto bbd = solo_rig::BehaviorPortsForKind(solo_rig::BehaviorKind::Bbd);
    REQUIRE(bbd.count == 2U);
    REQUIRE(bbd.ports[0].domain == solo_rig::BehaviorPortDomain::AudioSignal);
    const auto vca = solo_rig::BehaviorPortsForKind(solo_rig::BehaviorKind::Vca);
    REQUIRE(vca.count == 3U);
    REQUIRE(vca.ports[1].domain == solo_rig::BehaviorPortDomain::ControlSignal);
    const auto lfo = solo_rig::BehaviorPortsForKind(solo_rig::BehaviorKind::Lfo);
    REQUIRE(lfo.count == 1U);
    REQUIRE(lfo.ports[0].direction == solo_rig::BehaviorPortDirection::Output);
}

TEST_CASE("M18-D2e3 compiles explicit audio and control routes in deterministic topology order") {
    const auto result = solo_rig::CompileTypedBehaviorGraph(VcaWithLfo(), VcaWithLfoRoutes());
    REQUIRE(result.error == solo_rig::BehaviorPortCompileError::None);
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->nodes.size() == 2U);
    REQUIRE(result.plan->nodes[0].id == "lfo");
    REQUIRE(result.plan->nodes[1].id == "vca");
    REQUIRE(result.plan->routes.size() == 3U);
}

TEST_CASE("M18-D2e3 rejects unknown, duplicate, missing and direction-invalid ports") {
    auto routes = VcaWithLfoRoutes();
    routes[0].destination_port = "missing";
    REQUIRE(solo_rig::CompileTypedBehaviorGraph(VcaWithLfo(), routes).error ==
            solo_rig::BehaviorPortCompileError::UnknownPort);
    routes = VcaWithLfoRoutes();
    routes.push_back(routes[0]);
    REQUIRE(solo_rig::CompileTypedBehaviorGraph(VcaWithLfo(), routes).error ==
            solo_rig::BehaviorPortCompileError::DuplicateRoute);
    routes = VcaWithLfoRoutes();
    routes.erase(routes.begin() + 1);
    REQUIRE(solo_rig::CompileTypedBehaviorGraph(VcaWithLfo(), routes).error ==
            solo_rig::BehaviorPortCompileError::MissingPort);
    routes = VcaWithLfoRoutes();
    routes[0].source = "vca";
    routes[0].source_port = "audio_in";
    routes[0].destination = "lfo";
    routes[0].destination_port = "out";
    REQUIRE(solo_rig::CompileTypedBehaviorGraph(VcaWithLfo(), routes).error ==
            solo_rig::BehaviorPortCompileError::DirectionMismatch);
}

TEST_CASE("M18-D2e3 rejects incompatible domains, fan-in/fan-out, cycles and disconnected nodes") {
    auto nodes = VcaWithLfo();
    auto routes = VcaWithLfoRoutes();
    routes[0].destination_port = "control";
    routes.erase(routes.begin() + 1);
    REQUIRE(solo_rig::CompileTypedBehaviorGraph(nodes, routes).error ==
            solo_rig::BehaviorPortCompileError::IncompatibleConnection);

    routes = VcaWithLfoRoutes();
    routes.push_back({"input", "audio", "vca", "control"});
    REQUIRE(solo_rig::CompileTypedBehaviorGraph(nodes, routes).error ==
            solo_rig::BehaviorPortCompileError::FanOutUnsupported);

    const auto two_lfo_routes = std::vector<solo_rig::BehaviorPortRouteSpec>{
        {"input", "audio", "vca", "audio_in"}, {"lfo", "out", "vca", "control"},
        {"lfo2", "out", "vca", "control"}, {"vca", "out", "output", "audio"}};
    REQUIRE(solo_rig::CompileTypedBehaviorGraph(VcaWithTwoLfo(), two_lfo_routes).error ==
            solo_rig::BehaviorPortCompileError::FanInUnsupported);

    routes = VcaWithLfoRoutes();
    routes[0].source = "vca";
    routes[0].source_port = "audio_in";
    routes[0].destination = "lfo";
    routes[0].destination_port = "out";
    REQUIRE(solo_rig::CompileTypedBehaviorGraph(nodes, routes).error ==
            solo_rig::BehaviorPortCompileError::DirectionMismatch);

    nodes = {Node("a", "bbd", "generic-bbd-v1"), Node("b", "bbd", "generic-bbd-v1"),
        Node("s", "spring", "generic-spring-v1")};
    routes = {{"input", "audio", "s", "in"}, {"s", "out", "output", "audio"},
        {"a", "out", "b", "in"}, {"b", "out", "a", "in"}};
    REQUIRE(solo_rig::CompileTypedBehaviorGraph(nodes, routes).error ==
            solo_rig::BehaviorPortCompileError::CycleDetected);
}

TEST_CASE("M18-D2e3 rejects unauthorized models and capacity atomically") {
    auto nodes = VcaWithLfo();
    auto routes = VcaWithLfoRoutes();
    nodes[0].binding.model_id = "blocked.vca.template";
    REQUIRE(!solo_rig::CompileTypedBehaviorGraph(nodes, routes).plan.has_value());
    REQUIRE(solo_rig::CompileTypedBehaviorGraph(nodes, routes).error ==
            solo_rig::BehaviorPortCompileError::BlockedNode);

    nodes.clear();
    routes.clear();
    for (int index = 0; index < 17; ++index) {
        const auto id = "n" + std::to_string(index);
        nodes.push_back(Node(id, "vco", "generic-vco-v1"));
        routes.push_back({"input", "audio", id, "control"});
    }
    REQUIRE(solo_rig::CompileTypedBehaviorGraph(nodes, routes).error ==
            solo_rig::BehaviorPortCompileError::CapacityExceeded);
}
