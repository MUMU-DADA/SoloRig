#include "dsp/mna_solver.hpp"
#include "test_framework.hpp"

#include <cmath>

TEST_CASE("M18-C9b MNA updates BE/BC histories across oversampled steps and Reset") {
    solo_rig::CircuitNetlist circuit{
        .version = 2, .schema = "circuit", .reference_ground = "0",
        .nodes = {"collector", "base", "vcc", "0"},
        .ports = {{.id = "input", .direction = solo_rig::CircuitPortDirection::Input, .node = "base"},
                  {.id = "output", .direction = solo_rig::CircuitPortDirection::Output, .node = "collector"}},
        .supplies = {{.id = "vcc", .positive_node = "vcc", .negative_node = "0", .voltage = 5.0}},
        .components = {{.id = "load", .type = solo_rig::CircuitComponentType::Resistor,
                        .pins = {"vcc", "collector"}, .value = 1000.0},
                       {.id = "q1", .type = solo_rig::CircuitComponentType::Bjt,
                        .pins = {"collector", "base", "0"}, .initial = 0.0,
                        .bjt_model = solo_rig::CircuitBjtModel{
                            .kind = "generic-dynamic", .model_version = 2, .polarity = "npn",
                            .saturation_current = 1.0e-15, .forward_beta = 100.0,
                            .reverse_beta = 1.0, .thermal_voltage = 0.02585,
                            .max_current = 0.005, .max_step = 10.0,
                            .early_voltage = 40.0, .be_junction_capacitance = 1.0e-9,
                            .bc_junction_capacitance = 5.0e-10, .be_junction_potential = 0.7,
                            .bc_junction_potential = 0.6, .be_grading_coefficient = 0.5,
                            .bc_grading_coefficient = 0.5, .temperature_celsius = 25.0,
                            .leakage_temperature_coefficient = 0.0}}},
        .solver = {.method = "modified-nodal-analysis", .integration = "backward-euler", .oversampling = 4,
                   .convergence = {.max_iterations = 64, .absolute_tolerance = 1.0e-9,
                                   .relative_tolerance = 1.0e-6}}};
    auto compiled = solo_rig::CompileMnaCircuit(circuit);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(192000.0));
    const auto first = compiled.circuit->Solve(0.65);
    REQUIRE(first.Succeeded());
    REQUIRE(std::isfinite(first.output_voltage));
    const auto second = compiled.circuit->Solve(0.65);
    REQUIRE(second.Succeeded());
    compiled.circuit->Reset();
    const auto reset = compiled.circuit->Solve(0.65);
    REQUIRE(reset.Succeeded());
    REQUIRE(std::fabs(reset.output_voltage - first.output_voltage) < 1.0e-7);
}
