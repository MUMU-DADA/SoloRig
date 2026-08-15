#include "content/whitebox_mna_json.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace {

const char* DividerCircuit() {
    return R"({
        "version": 2,
        "schema": "circuit",
        "referenceGround": "0",
        "nodes": ["in", "out", "0"],
        "ports": [
            {"id":"input","direction":"input","node":"in"},
            {"id":"output","direction":"output","node":"out"}
        ],
        "supplies": [],
        "components": [
            {"id":"series","type":"resistor","pins":["in","out"],"value":1000},
            {"id":"load","type":"resistor","pins":["out","0"],"value":1000}
        ],
        "solver": {
            "method":"modified-nodal-analysis",
            "integration":"backward-euler",
            "oversampling":4,
            "convergence":{"maxIterations":16,"absoluteTolerance":1e-9,"relativeTolerance":1e-6}
        }
    })";
}

} // namespace

TEST_CASE("M20-A2b prepares circuit v2 JSON through the real MNA solver") {
    const auto prepared = solo_rig::PrepareWhiteboxMnaV2Json(DividerCircuit(), 192000.0);
    REQUIRE(prepared.error == solo_rig::WhiteboxMnaJsonError::None);
    REQUIRE(prepared.circuit.has_value());
    REQUIRE(prepared.load_errors.empty());
    REQUIRE(!prepared.compile_error.has_value());

    auto circuit = std::move(*prepared.circuit);
    const auto first = circuit.Solve(1.0);
    REQUIRE(first.error == solo_rig::MnaSolveError::None);
    REQUIRE(std::fabs(first.output_voltage - 0.5) < 1.0e-12);
    circuit.Reset();
    const auto after_reset = circuit.Solve(1.0);
    REQUIRE(after_reset.error == solo_rig::MnaSolveError::None);
    REQUIRE(std::fabs(after_reset.output_voltage - first.output_voltage) < 1.0e-12);
    static_assert(noexcept(circuit.Solve(0.0)));
}

TEST_CASE("M20-A2b preserves parse, compile and sample-rate refusal boundaries") {
    const auto invalid_json = solo_rig::PrepareWhiteboxMnaV2Json("not-json", 48000.0);
    REQUIRE(invalid_json.error == solo_rig::WhiteboxMnaJsonError::ParseFailure);
    REQUIRE(!invalid_json.load_errors.empty());
    REQUIRE(!invalid_json.circuit.has_value());

    auto unsupported = std::string(DividerCircuit());
    const auto type = std::string("resistor");
    unsupported.replace(unsupported.find(type), type.size(), "future-component");
    const auto compile_failure = solo_rig::PrepareWhiteboxMnaV2Json(unsupported, 48000.0);
    REQUIRE(compile_failure.error == solo_rig::WhiteboxMnaJsonError::ParseFailure);
    REQUIRE(!compile_failure.load_errors.empty());
    REQUIRE(compile_failure.load_errors.front() == solo_rig::CircuitLoadError::UnknownComponent);

    const auto invalid_rate = solo_rig::PrepareWhiteboxMnaV2Json(
        DividerCircuit(), std::numeric_limits<double>::quiet_NaN());
    REQUIRE(invalid_rate.error == solo_rig::WhiteboxMnaJsonError::InvalidSampleRate);
}

TEST_CASE("M20-A2b does not synthesize missing topology or silently repair it") {
    auto floating_graph = std::string(DividerCircuit());
    const auto nodes = std::string("[\"in\", \"out\", \"0\"]");
    floating_graph.replace(floating_graph.find(nodes), nodes.size(),
                           "[\"in\", \"out\", \"floating\", \"0\"]");
    const auto input_port = std::string("{\"id\":\"input\",\"direction\":\"input\",\"node\":\"in\"},\n            ");
    floating_graph.replace(floating_graph.find(input_port), input_port.size(), "");
    const auto load_pins = std::string("[\"out\",\"0\"]");
    floating_graph.replace(floating_graph.find(load_pins), load_pins.size(),
                           "[\"out\",\"floating\"]");
    const auto result = solo_rig::PrepareWhiteboxMnaV2Json(floating_graph, 48000.0);
    REQUIRE(result.error == solo_rig::WhiteboxMnaJsonError::None);
    REQUIRE(result.circuit.has_value());
    auto circuit = std::move(*result.circuit);
    const auto solve = circuit.Solve(1.0);
    REQUIRE(solve.error == solo_rig::MnaSolveError::SingularMatrix);
}
