#include "content/whitebox_compiler.hpp"
#include "test_framework.hpp"

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

void ReplaceOnce(std::string& source, std::string_view from, std::string_view to) {
    const auto position = source.find(from);
    if (position == std::string::npos) {
        throw std::runtime_error("D1b2 test mutation target missing");
    }
    source.replace(position, from.size(), to);
}

void RequireInvalid(std::string source, std::string_view from, std::string_view to) {
    ReplaceOnce(source, from, to);
    const auto result = solo_rig::CompileWhiteboxGraphV2Json(source);
    REQUIRE(!result.graph.has_value());
    REQUIRE(result.error == solo_rig::WhiteboxCompileError::InvalidSubcircuit);
}

} // namespace

TEST_CASE("M18-D1b2 parser accepts explicit inline passive port mapping") {
    const auto result = solo_rig::CompileWhiteboxGraphV2Json(ReadFixture("m18d1b2_inline_passive.json"));
    REQUIRE(result.error == std::nullopt);
    REQUIRE(result.graph.has_value());
    REQUIRE(result.graph->Subcircuits().empty());
    REQUIRE(result.graph->Components().size() == 4U);
    REQUIRE(result.graph->Nodes().size() == 4U);
    REQUIRE(result.graph->Components()[1].id == "passive-stage::series-r");
    REQUIRE(result.graph->Components()[2].id == "passive-stage::shunt-c");
    REQUIRE(result.graph->Components()[3].id == "passive-stage::series-l");
    REQUIRE(result.graph->Components()[2].initial == 0.25);
    REQUIRE(result.graph->Components()[3].initial == 0.005);
}

TEST_CASE("M18-D1b2 parser rejects ambiguous maps nodes and unsupported contents") {
    const auto valid = ReadFixture("m18d1b2_inline_passive.json");
    RequireInvalid(valid, "[\"input\", \"output\", \"ground\"]",
                   "[\"input\", \"input\", \"ground\"]");
    RequireInvalid(valid, "\"ground\": \"0\"", "\"extra\": \"0\"");
    RequireInvalid(valid, "\"ground\": \"0\"", "\"ground\": \"missing\"");
    RequireInvalid(valid, "\"ground\": \"0\"", "\"ground\": \"out\"");
    RequireInvalid(valid, "[\"mid\", \"ground\"]", "[\"mid\", \"floating\"]");
    RequireInvalid(valid, "\"id\": \"series-r\", \"kind\": \"resistor\"",
                   "\"id\": \"series-r\", \"kind\": \"diode\"");
    RequireInvalid(valid, "\"value\": 470.0", "\"value\": 0.0");
    RequireInvalid(valid, "\"id\": \"shunt-c\"", "\"id\": \"series-r\"");
    RequireInvalid(valid, "\"inline\": {", "\"modelReference\": \"external\", \"inline\": {");
    RequireInvalid(valid, "\"nodes\": [\"input\", \"mid\", \"output\", \"ground\"],",
                   "\"nodes\": [\"input\", \"mid\", \"output\", \"ground\"], \"subcircuits\": [],");
}

TEST_CASE("M18-D1b2 parser keeps legacy external subcircuits unresolved") {
    const auto result = solo_rig::CompileWhiteboxGraphV2Json(ReadFixture("m15_cross_network.json"));
    REQUIRE(result.graph.has_value());
    REQUIRE(result.graph->Subcircuits().size() == 1U);
    REQUIRE(result.graph->Subcircuits()[0].model_reference == "planned-stage");
}
