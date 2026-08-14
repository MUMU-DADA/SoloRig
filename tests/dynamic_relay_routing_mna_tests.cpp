#include "content/circuit_v2.hpp"
#include "dsp/mna_solver.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kRoutingNetlist = R"({
  "version":2,"schema":"circuit","referenceGround":"0",
  "nodes":["coil","rail","nc","no","0"],
  "ports":[{"id":"input","direction":"input","node":"coil"},
           {"id":"output","direction":"output","node":"no"}],
  "supplies":[{"id":"rail","positive":"rail","negative":"0","voltage":1}],
  "components":[
    {"id":"relay","type":"relay","pins":["coil","0","rail","nc","no"],"relayModel":{
      "kind":"generic-multi-contact","modelVersion":2,"poleCount":1,"contactGap":0.1,
      "coilResistance":100,"coilInductance":0.001,"openResistance":1000000,
      "closedResistance":10,"pullInCurrent":0.02,"dropOutCurrent":0.01,
      "operateTime":0.0001,"releaseTime":0.0002,"bounceDuration":0,"bounceFrequency":0,
      "bounceAmplitude":0,"maximumCoilCurrent":1,"initialState":"released"}},
    {"id":"nc-load","type":"resistor","pins":["nc","0"],"value":1000},
    {"id":"no-load","type":"resistor","pins":["no","0"],"value":1000}],
  "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4,
    "convergence":{"maxIterations":16,"absoluteTolerance":1e-10,"relativeTolerance":1e-8}}})";

solo_rig::MnaPreparedCircuit Compile(std::string_view source) {
    const auto parsed = solo_rig::ParseCircuitV2Json(source);
    if (!parsed.circuit.has_value()) {
        throw std::runtime_error("D1b3 netlist did not parse");
    }
    auto compiled = solo_rig::CompileMnaCircuit(*parsed.circuit);
    if (!compiled.circuit.has_value()) {
        throw std::runtime_error("D1b3 netlist did not compile");
    }
    return std::move(*compiled.circuit);
}

} // namespace

TEST_CASE("M18-D1b3 SPDT relay routes common from NC to NO and Reset restores released") {
    auto no_runtime = Compile(kRoutingNetlist);
    REQUIRE(no_runtime.Prepare(48000.0));
    REQUIRE(no_runtime.Solve(0.0).output_voltage < 0.01);
    double operated_no{};
    for (int index = 0; index < 200; ++index) {
        const auto result = no_runtime.Solve(5.0);
        REQUIRE(result.Succeeded());
        operated_no = result.output_voltage;
    }
    REQUIRE(operated_no > 0.95);
    no_runtime.Reset();
    REQUIRE(no_runtime.Solve(0.0).output_voltage < 0.01);

    auto nc_source = std::string(kRoutingNetlist);
    const auto output_position = nc_source.find("\"node\":\"no\"");
    REQUIRE(output_position != std::string::npos);
    nc_source.replace(output_position, std::string("\"node\":\"no\"").size(), "\"node\":\"nc\"");
    auto nc_runtime = Compile(nc_source);
    REQUIRE(nc_runtime.Prepare(48000.0));
    REQUIRE(nc_runtime.Solve(0.0).output_voltage > 0.95);
    double operated_nc{};
    for (int index = 0; index < 200; ++index) {
        const auto result = nc_runtime.Solve(5.0);
        REQUIRE(result.Succeeded());
        operated_nc = result.output_voltage;
    }
    REQUIRE(operated_nc < 0.01);
}

TEST_CASE("M18-D1b3 double pole relay parses fixed routing order at 192 kHz 4x") {
    const auto source = R"({"version":2,"schema":"circuit","referenceGround":"0",
      "nodes":["coil","rail","nc1","no1","nc2","no2","0"],
      "ports":[{"id":"input","direction":"input","node":"coil"},{"id":"output","direction":"output","node":"no2"}],
      "supplies":[{"id":"rail","positive":"rail","negative":"0","voltage":1}],"components":[
      {"id":"rly","type":"relay","pins":["coil","0","rail","nc1","no1","rail","nc2","no2"],
       "relayModel":{"kind":"generic-multi-contact","modelVersion":2,"poleCount":2,"contactGap":0.2,
       "coilResistance":100,"coilInductance":0.001,"openResistance":1000000,"closedResistance":10,
       "pullInCurrent":0.02,"dropOutCurrent":0.01,"operateTime":0.0001,"releaseTime":0.0002,
       "bounceDuration":0.001,"bounceFrequency":1000,"bounceAmplitude":0.1,
       "maximumCoilCurrent":1,"initialState":"released"}},
      {"id":"l1","type":"resistor","pins":["nc1","0"],"value":1000},
      {"id":"l2","type":"resistor","pins":["no1","0"],"value":1000},
      {"id":"l3","type":"resistor","pins":["nc2","0"],"value":1000},
      {"id":"l4","type":"resistor","pins":["no2","0"],"value":1000}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4,
      "convergence":{"maxIterations":16,"absoluteTolerance":1e-10,"relativeTolerance":1e-8}}})";
    const auto parsed = solo_rig::ParseCircuitV2Json(source);
    REQUIRE(parsed.circuit.has_value());
    REQUIRE(parsed.circuit->components.front().relay_model->pole_count == 2U);
    auto runtime = Compile(source);
    REQUIRE(runtime.Prepare(192000.0));
    for (int index = 0; index < 1000; ++index) {
        REQUIRE(runtime.Solve(5.0).Succeeded());
    }
    REQUIRE(runtime.Solve(5.0).output_voltage > 0.9);
    REQUIRE(runtime.Solve(std::numeric_limits<double>::quiet_NaN()).error ==
            solo_rig::MnaSolveError::NonFinite);
}

TEST_CASE("M18-D1b3 relay rejects pin count model mismatch singular and nonconvergent cases") {
    auto bad_pins = std::string(kRoutingNetlist);
    const auto pin = bad_pins.find(",\"no\"]");
    REQUIRE(pin != std::string::npos);
    bad_pins.erase(pin, std::string(",\"no\"").size());
    const auto bad_pins_result = solo_rig::ParseCircuitV2Json(bad_pins);
    REQUIRE(!bad_pins_result.errors.empty());
    REQUIRE(bad_pins_result.errors.front() == solo_rig::CircuitLoadError::InvalidPin ||
            bad_pins_result.errors.front() == solo_rig::CircuitLoadError::InvalidValue);

    auto bad_gap = std::string(kRoutingNetlist);
    const auto gap = bad_gap.find("\"contactGap\":0.1");
    REQUIRE(gap != std::string::npos);
    bad_gap.replace(gap, std::string("\"contactGap\":0.1").size(), "\"contactGap\":1");
    const auto bad_gap_result = solo_rig::ParseCircuitV2Json(bad_gap);
    REQUIRE(!bad_gap_result.errors.empty());
    REQUIRE(bad_gap_result.errors.front() == solo_rig::CircuitLoadError::InvalidValue);

    const auto parsed = solo_rig::ParseCircuitV2Json(kRoutingNetlist);
    REQUIRE(parsed.circuit.has_value());
    auto limited = *parsed.circuit;
    limited.solver.convergence.max_iterations = 1U;
    auto nonconvergent = solo_rig::CompileMnaCircuit(limited);
    REQUIRE(nonconvergent.circuit.has_value());
    REQUIRE(nonconvergent.circuit->Prepare(48000.0));
    REQUIRE(nonconvergent.circuit->Solve(5.0).error == solo_rig::MnaSolveError::NonConvergent);

    auto singular = *parsed.circuit;
    singular.supplies.clear();
    singular.components.erase(singular.components.begin() + 1, singular.components.end());
    auto floating = solo_rig::CompileMnaCircuit(singular);
    REQUIRE(floating.circuit.has_value());
    REQUIRE(floating.circuit->Prepare(48000.0));
    REQUIRE(floating.circuit->Solve(0.0).error == solo_rig::MnaSolveError::SingularMatrix);
}
