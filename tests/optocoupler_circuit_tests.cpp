#include "content/circuit_v2.hpp"
#include "dsp/mna_solver.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>

namespace {

constexpr const char* kOptocouplerCircuit = R"({
  "version":2,"schema":"circuit","referenceGround":"0",
  "nodes":["emitter","ldr","out","vcc","0"],
  "ports":[{"id":"input","direction":"input","node":"emitter"},
           {"id":"output","direction":"output","node":"out"}],
  "components":[
    {"id":"opto","type":"optocoupler","pins":["emitter","0","ldr","0"],
     "optocouplerModel":{"kind":"generic-electrical","modelVersion":1,
       "darkResistance":1000000,"litResistance":100,"emitterThreshold":1,
       "emitterScale":4,"responseExponent":1,"attackSeconds":0.001,
       "releaseSeconds":0.01,"maximumDrive":5,"initialIllumination":0,
       "emitterResistance":220}},
    {"id":"detector-vcc","type":"voltage-source","pins":["vcc","0"],"value":5},
    {"id":"ldr-load","type":"resistor","pins":["ldr","out"],"value":1000},
    {"id":"detector-feed","type":"resistor","pins":["vcc","out"],"value":1000},
    {"id":"out-return","type":"resistor","pins":["out","0"],"value":10000}
  ],
  "solver":{"method":"modified-nodal-analysis","integration":"backward-euler",
             "oversampling":1}
})";

} // namespace

TEST_CASE("M20-B4 parses and solves a four-terminal optical electrical component") {
    const auto parsed = solo_rig::ParseCircuitV2Json(kOptocouplerCircuit);
    REQUIRE(parsed.errors.empty());
    REQUIRE(parsed.circuit.has_value());
    REQUIRE(parsed.circuit->components.front().optocoupler_model.has_value());
    auto compiled = solo_rig::CompileMnaCircuit(*parsed.circuit);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(48000.0));
    const auto dark = compiled.circuit->Solve(0.0);
    const auto lit = compiled.circuit->Solve(5.0);
    REQUIRE(dark.Succeeded());
    REQUIRE(lit.Succeeded());
    REQUIRE(std::isfinite(dark.output_voltage));
    REQUIRE(std::isfinite(lit.output_voltage));
    REQUIRE(dark.output_voltage != lit.output_voltage);
    compiled.circuit->Reset();
    REQUIRE(compiled.circuit->Solve(0.0).output_voltage == dark.output_voltage);
    REQUIRE(compiled.circuit->Solve(5.0).output_voltage == lit.output_voltage);
    REQUIRE(compiled.circuit->Solve(std::numeric_limits<double>::quiet_NaN()).error ==
            solo_rig::MnaSolveError::NonFinite);
}

TEST_CASE("M20-B4 refuses malformed optical electrical model contracts") {
    const auto wrong_kind = solo_rig::ParseCircuitV2Json(
        std::string(kOptocouplerCircuit).replace(
            std::string(kOptocouplerCircuit).find("generic-electrical"), 18, "commercial-model"));
    REQUIRE(!wrong_kind.circuit.has_value());
    const auto wrong_pins = solo_rig::ParseCircuitV2Json(
        std::string(kOptocouplerCircuit).replace(
            std::string(kOptocouplerCircuit).find("[\"emitter\",\"0\",\"ldr\",\"0\"]"), 31,
            "[\"emitter\",\"0\",\"ldr\"]"));
    REQUIRE(!wrong_pins.circuit.has_value());
}
