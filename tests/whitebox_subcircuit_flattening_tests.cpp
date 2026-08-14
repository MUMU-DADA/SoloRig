#include "content/topology_decomposition.hpp"
#include "content/whitebox_compiler.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::string ReadFixture(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" / "whitebox" / name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open D1b2 fixture: " + path.string());
    }
    std::ostringstream source;
    source << stream.rdbuf();
    return source.str();
}

} // namespace

TEST_CASE("M18-D1b2 flattening produces deterministic prefixed graph IR") {
    const auto source = R"({"version":2,"schema":"circuit","referenceGround":"0",
      "nodes":["in","mid","out","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
      "supplies":[],"components":[],"subcircuits":[
        {"id":"first","ports":["a","b"],"portMap":{"a":"in","b":"mid"},"inline":{
          "nodes":["a","x","b"],"components":[
            {"id":"ra","kind":"resistor","pins":["a","x"],"value":100},
            {"id":"rb","kind":"resistor","pins":["x","b"],"value":200}]}},
        {"id":"second","ports":["a","b"],"portMap":{"a":"mid","b":"out"},"inline":{
          "nodes":["a","b"],"components":[{"id":"r","kind":"resistor","pins":["a","b"],"value":300}]}}
      ],"modelReferences":[],"backend":{"preferred":"mna"}})";
    const auto result = solo_rig::CompileWhiteboxGraphV2Json(source);
    REQUIRE(result.graph.has_value());
    REQUIRE(result.graph->Subcircuits().empty());
    REQUIRE(result.graph->Components().size() == 3U);
    REQUIRE(result.graph->Components()[0].id == "first::ra");
    REQUIRE(result.graph->Components()[1].id == "first::rb");
    REQUIRE(result.graph->Components()[2].id == "second::r");
    REQUIRE(std::any_of(result.graph->Nodes().begin(), result.graph->Nodes().end(), [](const auto& node) {
        return node.id == "first::x";
    }));
    REQUIRE(result.graph->Components()[0].pins[0] == "in");
    REQUIRE(result.graph->Components()[0].pins[1] == "first::x");
    REQUIRE(result.graph->Components()[2].pins[0] == "mid");
    REQUIRE(result.graph->Components()[2].pins[1] == "out");

    const auto analyzed = solo_rig::AnalyzeWhiteboxGraph(*result.graph);
    REQUIRE(analyzed.error == std::nullopt);
    REQUIRE(analyzed.plan.has_value());
}

TEST_CASE("M18-D1b2 flattening preserves the unresolved external boundary rejection") {
    const auto analyzed = solo_rig::AnalyzeWhiteboxGraphV2Json(ReadFixture("m15_cross_network.json"));
    REQUIRE(!analyzed.plan.has_value());
    REQUIRE(analyzed.error == solo_rig::WhiteboxDecompositionError::CrossNetworkBoundary);
}

TEST_CASE("M18-D1b2 flattening rejects expansion beyond the fixed graph limit") {
    std::ostringstream components;
    for (std::size_t index = 0; index < 257U; ++index) {
        if (index != 0U) {
            components << ',';
        }
        components << "{\"id\":\"r" << index
                   << "\",\"kind\":\"resistor\",\"pins\":[\"a\",\"b\"],\"value\":1}";
    }
    const auto source = std::string(R"({"version":2,"schema":"circuit","referenceGround":"0",
      "nodes":["in","out","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
      "supplies":[],"components":[],"subcircuits":[{"id":"large","ports":["a","b"],
      "portMap":{"a":"in","b":"out"},"inline":{"nodes":["a","b"],"components":[)" ) +
        components.str() + R"(]}}],"modelReferences":[],"backend":{"preferred":"mna"}})";
    const auto result = solo_rig::CompileWhiteboxGraphV2Json(source);
    REQUIRE(!result.graph.has_value());
    REQUIRE(result.error == solo_rig::WhiteboxCompileError::InvalidSubcircuit);
}
