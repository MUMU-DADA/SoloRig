#include "dsp/mna_solver.hpp"
#include "test_framework.hpp"

#include <cmath>

TEST_CASE("M18-C9c JFET MNA supports two gate histories, oversampling and Reset") {
    solo_rig::CircuitNetlist circuit{
        .version = 2, .schema = "circuit", .reference_ground = "0", .nodes = {"d", "g", "vcc", "0"},
        .ports = {{.id = "input", .direction = solo_rig::CircuitPortDirection::Input, .node = "g"},
                  {.id = "output", .direction = solo_rig::CircuitPortDirection::Output, .node = "d"}},
        .supplies = {{.id = "vcc", .positive_node = "vcc", .negative_node = "0", .voltage = 5.0}},
        .components = {{.id = "load", .type = solo_rig::CircuitComponentType::Resistor, .pins = {"vcc", "d"}, .value = 1000.0},
                       {.id = "j1", .type = solo_rig::CircuitComponentType::Jfet, .pins = {"d", "g", "0"}, .initial = 0.0,
                        .jfet_model = solo_rig::CircuitJfetModel{.kind = "generic-dynamic", .model_version = 2,
                            .polarity = "n-channel", .pinch_off_voltage = 1.0, .transconductance_parameter = 1.0e-3,
                            .max_current = 0.01, .max_step = 10.0, .channel_length_modulation = 0.1,
                            .temperature_celsius = 25.0, .gate_saturation_current = 1.0e-12,
                            .gate_thermal_voltage = 0.02585, .gate_junction_capacitance = 1.0e-9,
                            .gate_junction_potential = 0.7, .gate_grading_coefficient = 0.5,
                            .gate_leakage_temperature_coefficient = 0.0}}},
        .solver = {.oversampling = 4, .convergence = {.max_iterations = 64}}};
    auto compiled = solo_rig::CompileMnaCircuit(circuit);
    REQUIRE(compiled.circuit.has_value()); REQUIRE(compiled.circuit->Prepare(192000.0));
    const auto first = compiled.circuit->Solve(0.2); REQUIRE(first.Succeeded());
    compiled.circuit->Reset(); const auto reset = compiled.circuit->Solve(0.2); REQUIRE(reset.Succeeded());
    REQUIRE(std::fabs(first.output_voltage - reset.output_voltage) < 1.0e-7);
}
