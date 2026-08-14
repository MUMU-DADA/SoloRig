#include "content/circuit_v2.hpp"
#include "dsp/mna_solver.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>

namespace {

constexpr std::string_view kRelayNetlist=R"({
  "version":2,"schema":"circuit","referenceGround":"0","nodes":["coil","rail","out","0"],
  "ports":[{"id":"input","direction":"input","node":"coil"},{"id":"output","direction":"output","node":"out"}],
  "supplies":[{"id":"rail","positive":"rail","negative":"0","voltage":1}],
  "components":[
    {"id":"relay","type":"relay","pins":["coil","0","rail","out"],"relayModel":{
      "kind":"generic-dynamic","modelVersion":1,"coilResistance":100,"coilInductance":0.001,
      "openResistance":1000000,"closedResistance":10,"pullInCurrent":0.02,"dropOutCurrent":0.01,
      "operateTime":0.0001,"releaseTime":0.0002,"bounceDuration":0,"bounceFrequency":0,
      "bounceAmplitude":0,"maximumCoilCurrent":1,"initialState":"open"}},
    {"id":"load","type":"resistor","pins":["out","0"],"value":1000}],
  "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4,
    "convergence":{"maxIterations":16,"absoluteTolerance":1e-10,"relativeTolerance":1e-8}}})";

} // namespace

TEST_CASE("M18-D1b1 relay parses compiles and switches a contact from coil history") {
    const auto parsed=solo_rig::ParseCircuitV2Json(kRelayNetlist);
    REQUIRE(parsed.circuit.has_value());
    REQUIRE(parsed.circuit->components.front().relay_model.has_value());
    auto compiled=solo_rig::CompileMnaCircuit(*parsed.circuit);
    REQUIRE(compiled.circuit.has_value()); REQUIRE(compiled.circuit->Prepare(48000.0));
    auto open=compiled.circuit->Solve(0.0); REQUIRE(open.Succeeded()); REQUIRE(open.output_voltage<0.01);
    double closed{};
    for(int index=0;index<200;++index) {
        const auto result=compiled.circuit->Solve(5.0); REQUIRE(result.Succeeded()); closed=result.output_voltage;
    }
    REQUIRE(closed>0.95);
    double released=closed;
    for(int index=0;index<300;++index) {
        const auto result=compiled.circuit->Solve(0.0); REQUIRE(result.Succeeded()); released=result.output_voltage;
    }
    REQUIRE(released<0.05);
    compiled.circuit->Reset();
    REQUIRE(compiled.circuit->Solve(0.0).output_voltage<0.01);
}

TEST_CASE("M18-D1b1 relay advances at 192 kHz 4x and rejects finite-boundary failures") {
    const auto parsed=solo_rig::ParseCircuitV2Json(kRelayNetlist); REQUIRE(parsed.circuit.has_value());
    auto compiled=solo_rig::CompileMnaCircuit(*parsed.circuit); REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(192000.0));
    for(int index=0;index<1000;++index) REQUIRE(compiled.circuit->Solve(5.0).Succeeded());
    REQUIRE(compiled.circuit->Solve(std::numeric_limits<double>::quiet_NaN()).error==
        solo_rig::MnaSolveError::NonFinite);

    auto nonconvergent=*parsed.circuit;
    nonconvergent.solver.convergence.max_iterations=1;
    auto limited=solo_rig::CompileMnaCircuit(nonconvergent); REQUIRE(limited.circuit.has_value());
    REQUIRE(limited.circuit->Prepare(48000.0));
    REQUIRE(limited.circuit->Solve(5.0).error==solo_rig::MnaSolveError::NonConvergent);

    auto invalid=*parsed.circuit; invalid.components.front().relay_model->pull_in_current=0.0;
    REQUIRE(solo_rig::CompileMnaCircuit(invalid).error==solo_rig::MnaCompileError::InvalidValue);

    auto singular=*parsed.circuit;
    singular.supplies.clear();
    singular.components.erase(singular.components.begin()+1);
    auto floating=solo_rig::CompileMnaCircuit(singular); REQUIRE(floating.circuit.has_value());
    REQUIRE(floating.circuit->Prepare(48000.0));
    REQUIRE(floating.circuit->Solve(0.0).error==solo_rig::MnaSolveError::SingularMatrix);
}
