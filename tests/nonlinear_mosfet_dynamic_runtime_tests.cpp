#include "dsp/mna_solver.hpp"
#include "test_framework.hpp"

#include <cmath>

TEST_CASE("M18-C9d prepared MOSFET dynamic runtime remains finite at 192 kHz") {
    solo_rig::CircuitNetlist circuit{
        .version = 2, .schema = "circuit", .reference_ground = "0", .nodes = {"d", "g", "vcc", "0"},
        .ports = {{.id = "input", .direction = solo_rig::CircuitPortDirection::Input, .node = "g"},
                  {.id = "output", .direction = solo_rig::CircuitPortDirection::Output, .node = "d"}},
        .supplies = {{.id = "vcc", .positive_node = "vcc", .negative_node = "0", .voltage = 5.0}},
        .components = {{.id = "r", .type = solo_rig::CircuitComponentType::Resistor, .pins = {"vcc", "d"}, .value = 1000.0},
                       {.id = "m", .type = solo_rig::CircuitComponentType::Mosfet, .pins = {"d", "g", "0"},
                        .mosfet_model = solo_rig::CircuitMosfetModel{.kind = "generic-dynamic", .model_version = 2,
                            .polarity = "n-channel", .threshold_voltage = 1.0, .transconductance_parameter = 1.0e-3,
                            .max_current = 0.01, .max_step = 10.0, .channel_length_modulation = 0.05,
                            .subthreshold_current = 1.0e-12, .subthreshold_slope_voltage = 0.08,
                            .temperature_celsius = 25.0, .body_diode_saturation_current = 1.0e-12,
                            .body_diode_thermal_voltage = 0.02585, .gate_junction_capacitance = 1.0e-12,
                            .gate_junction_potential = 0.7, .gate_grading_coefficient = 0.5}}},
        .solver = {.oversampling = 4, .convergence = {.max_iterations = 32}}};
    auto compiled = solo_rig::CompileMnaCircuit(circuit); REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(192000.0));
    for (int i = 0; i < 16; ++i) {
        const auto result = compiled.circuit->Solve(0.2);
        REQUIRE(result.Succeeded()); REQUIRE(std::isfinite(result.output_voltage));
    }
    REQUIRE(solo_rig::IsRealtimeSafe(compiled.circuit->ProcessContract()));
}
