#include "content/behavior_graph_json.hpp"
#include "test_framework.hpp"

#include <string>

namespace {

const char* ValidGraph() {
    return R"({
        "version": 1,
        "schema": "solorig-behavior-graph",
        "nodes": [
            {"id":"vca","moduleId":"vca","kind":"vca","modelId":"generic-vca-v1"},
            {"id":"env","moduleId":"env","kind":"envelope","modelId":"generic-envelope-v1"}
        ],
        "routes": [
            {"source":"input","sourcePort":"audio","destination":"vca","destinationPort":"audio_in"},
            {"source":"input","sourcePort":"detector","destination":"env","destinationPort":"detector"},
            {"source":"env","sourcePort":"out","destination":"vca","destinationPort":"control"},
            {"source":"vca","sourcePort":"out","destination":"output","destinationPort":"audio"}
        ]
    })";
}

} // namespace

TEST_CASE("M20-A2a parses versioned typed behavior JSON into an executable plan") {
    const auto result = solo_rig::CompileTypedBehaviorGraphJson(ValidGraph());
    REQUIRE(result.error == solo_rig::BehaviorGraphJsonError::None);
    REQUIRE(result.graph_error == solo_rig::BehaviorPortCompileError::None);
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->nodes.size() == 2U);
    REQUIRE(result.plan->routes.size() == 4U);
    REQUIRE(result.plan->topological_order.size() == 2U);
}

TEST_CASE("M20-A2a preserves typed graph refusal boundaries during JSON lowering") {
    auto invalid_schema = std::string(ValidGraph());
    const auto schema = std::string("solorig-behavior-graph");
    invalid_schema.replace(invalid_schema.find(schema), schema.size(), "other");
    REQUIRE(solo_rig::CompileTypedBehaviorGraphJson(invalid_schema).error ==
            solo_rig::BehaviorGraphJsonError::InvalidSchema);

    auto blocked_model = std::string(ValidGraph());
    const auto model = std::string("generic-vca-v1");
    blocked_model.replace(blocked_model.find(model), model.size(), "blocked.vca.template");
    const auto blocked = solo_rig::CompileTypedBehaviorGraphJson(blocked_model);
    REQUIRE(blocked.error == solo_rig::BehaviorGraphJsonError::GraphCompileFailure);
    REQUIRE(blocked.graph_error == solo_rig::BehaviorPortCompileError::BlockedNode);

    auto missing_port = std::string(ValidGraph());
    const auto port = std::string("\"sourcePort\":\"detector\"");
    missing_port.replace(missing_port.find(port), port.size(), "\"missingPort\":\"detector\"");
    const auto invalid_route = solo_rig::CompileTypedBehaviorGraphJson(missing_port);
    REQUIRE(invalid_route.error == solo_rig::BehaviorGraphJsonError::InvalidRoute);
}

TEST_CASE("M20-A2a rejects malformed and unsupported JSON before graph compilation") {
    REQUIRE(solo_rig::CompileTypedBehaviorGraphJson("not-json").error ==
            solo_rig::BehaviorGraphJsonError::InvalidJson);
    REQUIRE(solo_rig::CompileTypedBehaviorGraphJson(
                "{\"version\":2,\"schema\":\"solorig-behavior-graph\",\"nodes\":[],\"routes\":[]}").error ==
            solo_rig::BehaviorGraphJsonError::UnsupportedVersion);
    REQUIRE(solo_rig::CompileTypedBehaviorGraphJson(
                "{\"version\":1,\"schema\":\"solorig-behavior-graph\",\"nodes\":[],\"routes\":[]}").error ==
            solo_rig::BehaviorGraphJsonError::MissingField);
}
