#include "m19c_representative_pedal_fixtures.hpp"

namespace solo_rig::m19c_fixtures {
namespace {

CircuitSolverConfig Solver(std::uint32_t oversampling) {
    CircuitSolverConfig solver{};
    solver.oversampling = oversampling;
    solver.convergence.max_iterations = 64U;
    solver.convergence.absolute_tolerance = 1.0e-9;
    solver.convergence.relative_tolerance = 1.0e-6;
    return solver;
}

CircuitNetlist Base(std::initializer_list<std::string_view> nodes,
                    std::string_view input_node, std::string_view output_node,
                    std::uint32_t oversampling) {
    CircuitNetlist circuit{};
    circuit.reference_ground = "0";
    circuit.nodes.assign(nodes.begin(), nodes.end());
    circuit.ports = {{.id = "input", .direction = CircuitPortDirection::Input,
                      .node = std::string(input_node)},
                     {.id = "output", .direction = CircuitPortDirection::Output,
                      .node = std::string(output_node)}};
    circuit.solver = Solver(oversampling);
    return circuit;
}

CircuitComponent Resistor(std::string_view id, std::string_view positive,
                          std::string_view negative, double value) {
    CircuitComponent component{};
    component.id = id;
    component.type = CircuitComponentType::Resistor;
    component.pins = {std::string(positive), std::string(negative)};
    component.value = value;
    return component;
}

CircuitComponent Capacitor(std::string_view id, std::string_view positive,
                           std::string_view negative, double value) {
    CircuitComponent component{};
    component.id = id;
    component.type = CircuitComponentType::Capacitor;
    component.pins = {std::string(positive), std::string(negative)};
    component.value = value;
    return component;
}

} // namespace

CircuitNetlist PassiveTone(std::uint32_t oversampling, double potentiometer_position) {
    auto circuit = Base({"in", "wiper", "out", "0"}, "in", "out", oversampling);
    CircuitComponent pot{};
    pot.id = "tone_pot";
    pot.type = CircuitComponentType::Potentiometer;
    pot.pins = {"in", "0", "wiper"};
    pot.value = 10000.0;
    pot.potentiometer = CircuitPotentiometerSpec{.position = potentiometer_position,
        .minimum = 0.0, .maximum = 1.0};
    circuit.components = {pot, Capacitor("tone_cap", "wiper", "0", 100.0e-9),
        Resistor("out_load", "wiper", "out", 1000.0),
        Resistor("out_return", "out", "0", 10000.0)};
    return circuit;
}

CircuitNetlist DiodeOverdrive(std::uint32_t oversampling) {
    auto circuit = Base({"in", "clip", "0"}, "in", "clip", oversampling);
    CircuitComponent diode{};
    diode.id = "clip_diode";
    diode.type = CircuitComponentType::Diode;
    diode.pins = {"clip", "0"};
    diode.diode_model = CircuitDiodeModel{.kind = "shockley", .model_version = 1U,
        .saturation_current = 1.0e-12, .thermal_voltage = 0.02585,
        .max_step = 1.0};
    circuit.components = {Resistor("drive", "in", "clip", 1000.0), diode,
        Resistor("bias", "clip", "0", 100000.0)};
    return circuit;
}

CircuitNetlist BjtBoostFuzz(std::uint32_t oversampling) {
    auto circuit = Base({"in", "base", "collector", "vcc", "0"}, "in", "collector", oversampling);
    circuit.supplies = {{.id = "vcc", .positive_node = "vcc", .negative_node = "0",
        .voltage = 5.0}};
    CircuitComponent transistor{};
    transistor.id = "q1";
    transistor.type = CircuitComponentType::Bjt;
    transistor.pins = {"collector", "base", "0"};
    transistor.bjt_model = CircuitBjtModel{.kind = "generic-dynamic", .model_version = 2U,
        .polarity = "npn", .saturation_current = 1.0e-15, .forward_beta = 100.0,
        .reverse_beta = 1.0, .thermal_voltage = 0.02585, .max_current = 0.01,
        .max_step = 10.0, .early_voltage = 40.0,
        .be_junction_capacitance = 1.0e-12, .bc_junction_capacitance = 1.0e-12,
        .be_junction_potential = 0.7, .bc_junction_potential = 0.6,
        .be_grading_coefficient = 0.5, .bc_grading_coefficient = 0.5,
        .temperature_celsius = 25.0};
    circuit.components = {Capacitor("input_coupling", "in", "base", 1.0e-6),
        Resistor("collector_load", "vcc", "collector", 1000.0),
        Resistor("base_bias_high", "vcc", "base", 100000.0),
        Resistor("base_bias_low", "base", "0", 100000.0), transistor};
    return circuit;
}

CircuitNetlist JfetBoostFuzz(std::uint32_t oversampling) {
    auto circuit = Base({"in", "gate", "drain", "vcc", "0"}, "in", "drain", oversampling);
    circuit.supplies = {{.id = "vcc", .positive_node = "vcc", .negative_node = "0",
        .voltage = 5.0}};
    CircuitComponent transistor{};
    transistor.id = "j1";
    transistor.type = CircuitComponentType::Jfet;
    transistor.pins = {"drain", "gate", "0"};
    transistor.jfet_model = CircuitJfetModel{.kind = "generic-dynamic", .model_version = 2U,
        .polarity = "n-channel", .pinch_off_voltage = 1.0,
        .transconductance_parameter = 1.0e-3, .max_current = 0.01,
        .max_step = 10.0, .channel_length_modulation = 0.1,
        .temperature_celsius = 25.0, .gate_saturation_current = 1.0e-12,
        .gate_thermal_voltage = 0.02585, .gate_junction_capacitance = 1.0e-9,
        .gate_junction_potential = 0.7, .gate_grading_coefficient = 0.5};
    circuit.components = {Capacitor("input_coupling", "in", "gate", 1.0e-6),
        Resistor("drain_load", "vcc", "drain", 1000.0),
        Resistor("gate_bias", "gate", "0", 1000000.0), transistor};
    return circuit;
}

CircuitNetlist MosfetBoostFuzz(std::uint32_t oversampling) {
    auto circuit = Base({"in", "gate", "drain", "vcc", "0"}, "in", "drain", oversampling);
    circuit.supplies = {{.id = "vcc", .positive_node = "vcc", .negative_node = "0",
        .voltage = 5.0}};
    CircuitComponent transistor{};
    transistor.id = "m1";
    transistor.type = CircuitComponentType::Mosfet;
    transistor.pins = {"drain", "gate", "0"};
    transistor.mosfet_model = CircuitMosfetModel{.kind = "generic-dynamic", .model_version = 2U,
        .polarity = "n-channel", .threshold_voltage = 1.0,
        .transconductance_parameter = 1.0e-3, .max_current = 0.01,
        .max_step = 10.0, .channel_length_modulation = 0.05,
        .subthreshold_current = 1.0e-12, .subthreshold_slope_voltage = 0.08,
        .temperature_celsius = 25.0, .body_diode_saturation_current = 1.0e-12,
        .body_diode_thermal_voltage = 0.02585, .gate_junction_capacitance = 1.0e-9,
        .gate_junction_potential = 0.7, .gate_grading_coefficient = 0.5};
    circuit.components = {Capacitor("input_coupling", "in", "gate", 1.0e-6),
        Resistor("drain_load", "vcc", "drain", 1000.0),
        Resistor("gate_bias", "gate", "0", 1000000.0), transistor};
    return circuit;
}

CircuitNetlist DynamicOpampDrive(std::uint32_t oversampling) {
    auto circuit = Base({"in", "out", "0"}, "in", "out", oversampling);
    CircuitComponent opamp{};
    opamp.id = "a";
    opamp.type = CircuitComponentType::OpAmp;
    opamp.pins = {"out", "0", "in", "0"};
    opamp.amplifier_model = CircuitAmplifierModel{.kind = "generic-dynamic", .model_version = 2U,
        .gain = 100.0, .minimum_output = -10.0, .maximum_output = 10.0,
        .max_step = 1.0, .gain_bandwidth_hz = 1000.0, .slew_rate = 1000000.0,
        .input_resistance = 1000000.0, .input_bias_current = 2.0e-6,
        .input_offset = 0.01, .output_resistance = 100.0,
        .output_current_limit = 0.1, .supply_minimum = -12.0,
        .supply_maximum = 12.0};
    circuit.components = {opamp, Resistor("load", "out", "0", 1000.0)};
    return circuit;
}

} // namespace solo_rig::m19c_fixtures
