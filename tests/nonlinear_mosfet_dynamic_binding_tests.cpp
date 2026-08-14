#include "content/circuit_v2.hpp"
#include "content/model_database.hpp"
#include "dsp/mna_solver.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {
std::string ReadFixture(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" / "whitebox" / name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open C9d fixture: " + path.string());
    std::ostringstream source; source << stream.rdbuf(); return source.str();
}

constexpr std::string_view kInline = R"({
  "version":2,"schema":"circuit","referenceGround":"0","nodes":["vdd","d","g","0"],
  "ports":[{"id":"input","direction":"input","node":"g"},{"id":"output","direction":"output","node":"d"}],
  "supplies":[{"id":"supply","positive":"vdd","negative":"0","voltage":5.0}],
  "components":[{"id":"m","type":"mosfet","pins":["d","g","0"],"mosfetModel":{
    "kind":"generic-dynamic","modelVersion":2,"polarity":"n-channel","thresholdVoltage":1.0,
    "transconductanceParameter":0.001,"maxCurrent":0.01,"maxStep":0.5,
    "channelLengthModulation":0.05,"subthresholdCurrent":1.0e-12,"subthresholdSlopeVoltage":0.08,
    "temperatureCelsius":25.0,"bodyDiodeSaturationCurrent":1.0e-12,"bodyDiodeThermalVoltage":0.02585,
    "gateJunctionCapacitance":1.0e-12,"gateJunctionPotential":0.7,"gateGradingCoefficient":0.5}},
    {"id":"rd","type":"resistor","pins":["vdd","d"],"value":1000.0}],
  "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4,
    "convergence":{"maxIterations":64,"absoluteTolerance":1.0e-10,"relativeTolerance":1.0e-8}}
})";
}

TEST_CASE("M18-C9d inline and audited N/P MOSFET references reach dynamic Prepare") {
    const auto inline_graph = solo_rig::ParseCircuitV2Json(kInline);
    REQUIRE(inline_graph.errors.empty());
    REQUIRE(inline_graph.circuit.has_value());
    const auto database = solo_rig::BuildModelDatabase(ReadFixture("m18c9d_cleanroom_mosfet_n_model.json"));
    REQUIRE(database.error == std::nullopt); REQUIRE(database.database.has_value());
    auto audited_source = std::string(kInline);
    const auto marker = audited_source.find("\"mosfetModel\":{");
    REQUIRE(marker != std::string::npos);
    const auto object_end = audited_source.find("}},\n    {\"id\":\"rd\"", marker);
    REQUIRE(object_end != std::string::npos);
    audited_source.replace(marker, object_end - marker + 2,
        "\"model\":\"solorig.cleanroom.mosfet.n-channel.dynamic.v2\"}");
    const auto audited_graph = solo_rig::ParseCircuitV2Json(audited_source);
    REQUIRE(audited_graph.errors.empty()); REQUIRE(audited_graph.circuit.has_value());
    auto compiled = solo_rig::CompileMnaCircuit(*audited_graph.circuit, *database.database);
    REQUIRE(compiled.circuit.has_value()); REQUIRE(compiled.circuit->Prepare(192000.0));
    REQUIRE(compiled.circuit->Solve(0.2).Succeeded());
}

TEST_CASE("M18-C9d dynamic MOSFET audit contract rejects incomplete parameter sets") {
    auto source = ReadFixture("m18c9d_cleanroom_mosfet_n_model.json");
    const auto marker = source.find("\"subthreshold-current\"");
    REQUIRE(marker != std::string::npos);
    const auto end = source.find("\n    {", marker);
    REQUIRE(end != std::string::npos);
    source.erase(marker, end - marker);
    REQUIRE(solo_rig::BuildModelDatabase(source).error.has_value());
}
