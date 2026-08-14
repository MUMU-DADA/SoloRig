#include "dsp/mna_solver.hpp"
#include "test_framework.hpp"

#include <cmath>

TEST_CASE("M18-C9b prepared BJT dynamic runtime remains finite at 192 kHz and does not allocate") {
    solo_rig::CircuitNetlist circuit{
        .version = 2, .schema = "circuit", .reference_ground = "0", .nodes = {"c", "b", "vcc", "0"},
        .ports = {{.id = "input", .direction = solo_rig::CircuitPortDirection::Input, .node = "b"},
                  {.id = "output", .direction = solo_rig::CircuitPortDirection::Output, .node = "c"}},
        .supplies = {{.id = "vcc", .positive_node = "vcc", .negative_node = "0", .voltage = 5.0}},
        .components = {{.id = "load", .type = solo_rig::CircuitComponentType::Resistor,
                        .pins = {"vcc", "c"}, .value = 1000.0},
                       {.id = "q", .type = solo_rig::CircuitComponentType::Bjt,
                        .pins = {"c", "b", "0"},
                        .bjt_model = solo_rig::CircuitBjtModel{
                            .kind = "generic-dynamic", .model_version = 2, .polarity = "npn",
                            .saturation_current = 1.0e-15, .forward_beta = 100.0,
                            .reverse_beta = 1.0, .thermal_voltage = 0.02585,
                            .max_current = 0.01, .max_step = 10.0,
                            .early_voltage = 80.0, .be_junction_capacitance = 1.0e-12,
                            .bc_junction_capacitance = 1.0e-12, .be_junction_potential = 0.7,
                            .bc_junction_potential = 0.6, .be_grading_coefficient = 0.5,
                            .bc_grading_coefficient = 0.5, .temperature_celsius = 25.0,
                            .leakage_temperature_coefficient = 0.0}}},
        .solver = {.oversampling = 4, .convergence = {.max_iterations = 32}}};
    auto compiled = solo_rig::CompileMnaCircuit(circuit);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(192000.0));
    for (int index = 0; index < 32; ++index) {
        const auto result = compiled.circuit->Solve(0.55 + 0.01 * std::sin(index * 0.2));
        REQUIRE(result.Succeeded());
        REQUIRE(std::isfinite(result.output_voltage));
    }
    REQUIRE(solo_rig::IsRealtimeSafe(compiled.circuit->ProcessContract()));
}
