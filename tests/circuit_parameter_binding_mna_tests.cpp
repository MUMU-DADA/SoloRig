#include "content/circuit_v2.hpp"
#include "dsp/mna_solver.hpp"
#include "test_framework.hpp"

#include <cmath>

namespace {

solo_rig::CircuitNetlist ControlledDivider(double smoothing_seconds = 0.0, std::uint32_t oversampling = 1U) {
    solo_rig::CircuitNetlist circuit{
        .version = 2, .schema = "circuit", .reference_ground = "0",
        .nodes = {"in", "out", "0"},
        .ports = {{.id="input", .direction=solo_rig::CircuitPortDirection::Input, .node="in"},
                  {.id="output", .direction=solo_rig::CircuitPortDirection::Output, .node="out"}},
        .components = {{.id="pot", .type=solo_rig::CircuitComponentType::Potentiometer,
                        .pins={"in","0","out"}, .value=10000.0,
                        .potentiometer=solo_rig::CircuitPotentiometerSpec{.position=0.25,.minimum=0,.maximum=1}}},
        .parameter_bindings = {{.parameter_id="position", .target_id="pot",
            .property=solo_rig::CircuitParameterBindingProperty::PotentiometerPosition,
            .smoothing_seconds=smoothing_seconds}},
        .solver = {.oversampling=oversampling}};
    return circuit;
}

} // namespace

TEST_CASE("M18-D1a MNA updates a compiled potentiometer without recompilation and Reset restores initial") {
    auto compiled = solo_rig::CompileMnaCircuit(ControlledDivider());
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->ControlCount() == 1U);
    REQUIRE(compiled.circuit->FindControl("position") == 0U);
    REQUIRE(compiled.circuit->Prepare(48000.0));
    REQUIRE(std::fabs(compiled.circuit->Solve(1.0).output_voltage - 0.75) < 1.0e-8);
    REQUIRE(compiled.circuit->SetControlTarget(0U, 0.75));
    REQUIRE(std::fabs(compiled.circuit->Solve(1.0).output_voltage - 0.25) < 1.0e-8);
    REQUIRE(!compiled.circuit->SetControlTarget(0U, 2.0));
    REQUIRE(!compiled.circuit->SetControlTarget(0U, NAN));
    compiled.circuit->Reset();
    REQUIRE(std::fabs(compiled.circuit->Solve(1.0).output_voltage - 0.75) < 1.0e-8);
}

TEST_CASE("M18-D1a MNA smooths controls at each 192 kHz 4x substep") {
    auto compiled = solo_rig::CompileMnaCircuit(ControlledDivider(0.001, 4U));
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(192000.0));
    REQUIRE(compiled.circuit->SetControlTarget(0U, 0.75));
    const auto first = compiled.circuit->Solve(1.0);
    REQUIRE(first.Succeeded());
    REQUIRE(first.output_voltage < 0.75);
    REQUIRE(first.output_voltage > 0.25);
    REQUIRE(compiled.circuit->ControlValue(0U).has_value());
    REQUIRE(*compiled.circuit->ControlValue(0U) > 0.25);
    REQUIRE(*compiled.circuit->ControlValue(0U) < 0.75);
}

TEST_CASE("M18-D1a finite switch endpoints and supply voltage share the prepared control path") {
    solo_rig::CircuitNetlist circuit{
        .version=2,.schema="circuit",.reference_ground="0",.nodes={"rail","out","0"},
        .ports={{.id="output",.direction=solo_rig::CircuitPortDirection::Output,.node="out"}},
        .supplies={{.id="rail",.positive_node="rail",.negative_node="0",.voltage=9.0}},
        .components={{.id="series",.type=solo_rig::CircuitComponentType::Resistor,.pins={"rail","out"},.value=1000.0},
                     {.id="sw",.type=solo_rig::CircuitComponentType::Switch,.pins={"out","0"},
                      .switch_spec=solo_rig::CircuitSwitchSpec{.states={"open","closed"},.initial_state=0,
                          .open_resistance=1000000.0,.closed_resistance=1000.0}}},
        .parameter_bindings={{.parameter_id="engaged",.target_id="sw",.property=solo_rig::CircuitParameterBindingProperty::SwitchState},
                             {.parameter_id="voltage",.target_id="rail",.property=solo_rig::CircuitParameterBindingProperty::SupplyVoltage}},
        .solver={}};
    auto compiled=solo_rig::CompileMnaCircuit(circuit);
    REQUIRE(compiled.circuit.has_value()); REQUIRE(compiled.circuit->Prepare(48000.0));
    const auto open=compiled.circuit->Solve(0.0); REQUIRE(open.Succeeded()); REQUIRE(open.output_voltage>8.9);
    REQUIRE(compiled.circuit->SetControlTarget(0U,1.0)); REQUIRE(compiled.circuit->SetControlTarget(1U,6.0));
    const auto closed=compiled.circuit->Solve(0.0); REQUIRE(closed.Succeeded());
    REQUIRE(std::fabs(closed.output_voltage-3.0)<1.0e-6);
}
