#include "content/circuit_v2.hpp"
#include "dsp/dynamic_speaker_load.hpp"
#include "dsp/mna_solver.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>

namespace {

std::string ReadFixture(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" / "whitebox" / name;
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

solo_rig::DynamicSpeakerLoadModel SpeakerModel() {
    return {6.0, 5.0e-4, 5.0, 0.02, 2.0, 5.0e-4, 1000.0, 0.002, 10.0, 100.0, 0.1};
}

double ReferenceFirstStep(double input, double sample_rate) {
    double current{};
    for (int iteration = 0; iteration < 64; ++iteration) {
        const auto speaker = solo_rig::EvaluateDynamicSpeakerLoad(
            SpeakerModel(), current, {}, 1.0 / sample_rate);
        const double residual = 2.0 * current + speaker.voltage - input;
        const double derivative = 2.0 + speaker.voltage_derivative_current;
        const double next = current - residual / derivative;
        if (std::fabs(next - current) < 1.0e-14) return speaker.voltage;
        current = next;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

solo_rig::CircuitSpeakerLoadModel InlineSpeaker() {
    return {.kind = "electrodynamic", .model_version = 1,
            .voice_coil_resistance = 6.0, .voice_coil_inductance = 5.0e-4,
            .force_factor = 5.0, .moving_mass = 0.02, .mechanical_resistance = 2.0,
            .compliance = 5.0e-4, .nonlinear_stiffness = 1000.0,
            .nonlinear_displacement_scale = 0.002, .maximum_current = 10.0,
            .maximum_velocity = 100.0, .maximum_displacement = 0.1};
}

} // namespace

TEST_CASE("M18-C10b speaker fixture parses and MNA matches the branch reference after Reset") {
    const auto parsed = solo_rig::ParseCircuitV2Json(
        ReadFixture("m18c10b_dynamic_speaker_inline.json"));
    REQUIRE(parsed.circuit.has_value());
    auto compiled = solo_rig::CompileMnaCircuit(*parsed.circuit);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(48000.0));
    const auto first = compiled.circuit->Solve(1.0);
    REQUIRE(first.Succeeded());
    REQUIRE(std::fabs(first.output_voltage - ReferenceFirstStep(1.0, 48000.0)) < 1.0e-9);
    compiled.circuit->Reset();
    const auto reset = compiled.circuit->Solve(1.0);
    REQUIRE(reset.Succeeded());
    REQUIRE(std::fabs(reset.output_voltage - first.output_voltage) < 1.0e-12);

    const auto invalid = solo_rig::ParseCircuitV2Json(
        ReadFixture("m18c10b_invalid_dynamic_speaker.json"));
    REQUIRE(!invalid.circuit.has_value());
    REQUIRE(invalid.errors.front() == solo_rig::CircuitLoadError::InvalidValue);
}

TEST_CASE("M18-C10b speaker states advance at 192 kHz 4x and independent initials Reset") {
    auto parsed = solo_rig::ParseCircuitV2Json(ReadFixture("m18c10b_dynamic_speaker_inline.json"));
    REQUIRE(parsed.circuit.has_value());
    parsed.circuit->solver.oversampling = 4U;
    auto& model = *parsed.circuit->components[1].speaker_load_model;
    model.initial_current = 0.1;
    model.initial_velocity = 0.02;
    model.initial_displacement = 0.001;
    auto compiled = solo_rig::CompileMnaCircuit(*parsed.circuit);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(192000.0));
    const auto first = compiled.circuit->Solve(0.5);
    const auto second = compiled.circuit->Solve(0.5);
    REQUIRE(first.Succeeded());
    REQUIRE(second.Succeeded());
    REQUIRE(std::isfinite(first.output_voltage));
    REQUIRE(std::fabs(second.output_voltage - first.output_voltage) > 1.0e-10);
    compiled.circuit->Reset();
    const auto reset = compiled.circuit->Solve(0.5);
    REQUIRE(reset.Succeeded());
    REQUIRE(std::fabs(reset.output_voltage - first.output_voltage) < 1.0e-12);
    REQUIRE(solo_rig::IsRealtimeSafe(compiled.circuit->ProcessContract()));
}

TEST_CASE("M18-C10b existing dynamic opamp feedback jointly solves the speaker load") {
    const auto make = [](double force_factor) {
        solo_rig::CircuitNetlist circuit{
            .version = 2, .schema = "circuit", .reference_ground = "0",
            .nodes = {"in", "out", "feedback", "0"},
            .ports = {{.id = "input", .direction = solo_rig::CircuitPortDirection::Input, .node = "in"},
                      {.id = "output", .direction = solo_rig::CircuitPortDirection::Output, .node = "out"}},
            .components = {
                {.id = "amp", .type = solo_rig::CircuitComponentType::OpAmp,
                 .pins = {"out", "0", "in", "feedback"},
                 .amplifier_model = solo_rig::CircuitAmplifierModel{
                     .kind = "generic-dynamic", .model_version = 2, .gain = 10000.0,
                     .minimum_output = -20.0, .maximum_output = 20.0, .max_step = 1.0,
                     .gain_bandwidth_hz = 1.0e6, .slew_rate = 1.0e7,
                     .input_resistance = 1.0e9, .output_resistance = 0.5,
                     .output_current_limit = 10.0, .supply_minimum = -24.0,
                     .supply_maximum = 24.0}},
                {.id = "rf", .type = solo_rig::CircuitComponentType::Resistor,
                 .pins = {"out", "feedback"}, .value = 1000.0},
                {.id = "rg", .type = solo_rig::CircuitComponentType::Resistor,
                 .pins = {"feedback", "0"}, .value = 1000.0},
                {.id = "speaker", .type = solo_rig::CircuitComponentType::SpeakerLoad,
                 .pins = {"out", "0"}, .speaker_load_model = InlineSpeaker()}},
            .solver = {.oversampling = 4, .convergence = {.max_iterations = 64}}};
        circuit.components.back().speaker_load_model->force_factor = force_factor;
        return circuit;
    };
    auto electrical = solo_rig::CompileMnaCircuit(make(0.0));
    auto electromechanical = solo_rig::CompileMnaCircuit(make(5.0));
    REQUIRE(electrical.circuit.has_value());
    REQUIRE(electromechanical.circuit.has_value());
    REQUIRE(electrical.circuit->Prepare(48000.0));
    REQUIRE(electromechanical.circuit->Prepare(48000.0));
    solo_rig::MnaSolveResult electrical_result;
    solo_rig::MnaSolveResult electromechanical_result;
    for (int sample = 0; sample < 32; ++sample) {
        electrical_result = electrical.circuit->Solve(1.0);
        electromechanical_result = electromechanical.circuit->Solve(1.0);
        REQUIRE(electrical_result.Succeeded());
        REQUIRE(electromechanical_result.Succeeded());
    }
    REQUIRE(electrical_result.output_voltage > 1.9);
    REQUIRE(electromechanical_result.output_voltage > 1.9);
    REQUIRE(std::fabs(electrical_result.output_voltage -
                      electromechanical_result.output_voltage) > 1.0e-8);
}

TEST_CASE("M18-C10b speaker MNA exposes nonfinite singular and bounded nonconvergence") {
    auto parsed = solo_rig::ParseCircuitV2Json(ReadFixture("m18c10b_dynamic_speaker_inline.json"));
    REQUIRE(parsed.circuit.has_value());
    parsed.circuit->solver.convergence.max_iterations = 1U;
    auto nonconvergent = solo_rig::CompileMnaCircuit(*parsed.circuit);
    REQUIRE(nonconvergent.circuit->Prepare(48000.0));
    REQUIRE(nonconvergent.circuit->Solve(10.0).error == solo_rig::MnaSolveError::NonConvergent);
    REQUIRE(nonconvergent.circuit->Solve(std::numeric_limits<double>::infinity()).error ==
            solo_rig::MnaSolveError::NonFinite);

    parsed.circuit->nodes = {"in", "a", "b", "0"};
    parsed.circuit->ports[1].node = "a";
    parsed.circuit->components[0].pins = {"in", "0"};
    parsed.circuit->components[1].pins = {"a", "b"};
    auto singular = solo_rig::CompileMnaCircuit(*parsed.circuit);
    REQUIRE(singular.circuit.has_value());
    REQUIRE(singular.circuit->Prepare(48000.0));
    REQUIRE(singular.circuit->Solve(0.0).error == solo_rig::MnaSolveError::SingularMatrix);
}
