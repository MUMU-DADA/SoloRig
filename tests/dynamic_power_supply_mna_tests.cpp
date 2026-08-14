#include "content/circuit_v2.hpp"
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

solo_rig::CircuitNetlist SupplyCircuit(std::uint32_t oversampling = 1U,
                                       std::uint32_t max_iterations = 32U) {
    return {
        .version = 2,
        .schema = "circuit",
        .reference_ground = "0",
        .nodes = {"rail", "0"},
        .ports = {{.id = "output", .direction = solo_rig::CircuitPortDirection::Output,
                   .node = "rail"}},
        .supplies = {{.id = "supply", .positive_node = "rail", .negative_node = "0",
                      .voltage = 12.0, .kind = "rectified-reservoir", .model_version = 1,
                      .source_resistance = 10.0, .sag_resistance = 20.0,
                      .recovery_time_seconds = 0.1, .ripple_amplitude = 0.0,
                      .ripple_frequency_hz = 100.0, .maximum_sag = 4.0,
                      .maximum_load_current = 1.0, .initial_sag = 0.0}},
        .components = {{.id = "load", .type = solo_rig::CircuitComponentType::Resistor,
                        .pins = {"rail", "0"}, .value = 1000.0}},
        .solver = {.oversampling = oversampling,
                   .convergence = {.max_iterations = max_iterations,
                                   .absolute_tolerance = 1.0e-12,
                                   .relative_tolerance = 1.0e-10}}};
}

} // namespace

TEST_CASE("M18-C10a parser accepts the complete supply contract and rejects invalid initial Sag") {
    const auto valid = solo_rig::ParseCircuitV2Json(
        ReadFixture("m18c10a_rectified_reservoir_supply.json"));
    REQUIRE(valid.circuit.has_value());
    REQUIRE(valid.circuit->supplies.size() == 1U);
    REQUIRE(valid.circuit->supplies[0].kind == "rectified-reservoir");
    const auto invalid = solo_rig::ParseCircuitV2Json(
        ReadFixture("m18c10a_invalid_rectified_reservoir_supply.json"));
    REQUIRE(!invalid.circuit.has_value());
    REQUIRE(invalid.errors.front() == solo_rig::CircuitLoadError::InvalidValue);
}

TEST_CASE("M18-C10a dynamic supply MNA matches independent first-step backward-Euler reference") {
    auto compiled = solo_rig::CompileMnaCircuit(SupplyCircuit());
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(1000.0));
    const auto result = compiled.circuit->Solve(0.0);
    REQUIRE(result.Succeeded());
    constexpr double alpha = 0.001 / 0.101;
    constexpr double expected = 12.0 / (1.0 + (10.0 + alpha * 20.0) / 1000.0);
    REQUIRE(std::fabs(result.output_voltage - expected) < 1.0e-10);

    compiled.circuit->Reset();
    const auto reset = compiled.circuit->Solve(0.0);
    REQUIRE(reset.Succeeded());
    REQUIRE(std::fabs(reset.output_voltage - expected) < 1.0e-10);
}

TEST_CASE("M18-C10a dynamic supply updates every oversampled step and preserves ideal supplies") {
    auto dynamic = solo_rig::CompileMnaCircuit(SupplyCircuit(4U));
    REQUIRE(dynamic.circuit.has_value());
    REQUIRE(dynamic.circuit->Prepare(192000.0));
    const auto first = dynamic.circuit->Solve(0.0);
    const auto second = dynamic.circuit->Solve(0.0);
    REQUIRE(first.Succeeded());
    REQUIRE(second.Succeeded());
    REQUIRE(std::isfinite(first.output_voltage));
    REQUIRE(second.output_voltage < first.output_voltage);
    dynamic.circuit->Reset();
    const auto reset = dynamic.circuit->Solve(0.0);
    REQUIRE(reset.Succeeded());
    REQUIRE(std::fabs(reset.output_voltage - first.output_voltage) < 1.0e-12);

    auto ideal_netlist = SupplyCircuit();
    ideal_netlist.supplies[0] = {.id = "supply", .positive_node = "rail",
                                 .negative_node = "0", .voltage = 12.0};
    auto ideal = solo_rig::CompileMnaCircuit(ideal_netlist);
    REQUIRE(ideal.circuit.has_value());
    REQUIRE(ideal.circuit->Prepare(1000.0));
    const auto ideal_result = ideal.circuit->Solve(0.0);
    REQUIRE(ideal_result.Succeeded());
    REQUIRE(std::fabs(ideal_result.output_voltage - 12.0) < 1.0e-12);
}

TEST_CASE("M18-C10a dynamic supply rejects invalid nonfinite nonconvergent and singular boundaries") {
    auto invalid = SupplyCircuit();
    invalid.supplies[0].initial_sag = 5.0;
    REQUIRE(solo_rig::CompileMnaCircuit(invalid).error == solo_rig::MnaCompileError::InvalidValue);

    auto nonconvergent = solo_rig::CompileMnaCircuit(SupplyCircuit(1U, 1U));
    REQUIRE(nonconvergent.circuit.has_value());
    REQUIRE(nonconvergent.circuit->Prepare(48000.0));
    REQUIRE(nonconvergent.circuit->Solve(0.0).error == solo_rig::MnaSolveError::NonConvergent);

    auto with_input = SupplyCircuit();
    with_input.nodes.insert(with_input.nodes.begin(), "in");
    with_input.ports.insert(with_input.ports.begin(),
        {.id = "input", .direction = solo_rig::CircuitPortDirection::Input, .node = "in"});
    with_input.components.push_back({.id = "input-load",
        .type = solo_rig::CircuitComponentType::Resistor, .pins = {"in", "0"}, .value = 1000.0});
    auto nonfinite = solo_rig::CompileMnaCircuit(with_input);
    REQUIRE(nonfinite.circuit.has_value());
    REQUIRE(nonfinite.circuit->Prepare(48000.0));
    REQUIRE(nonfinite.circuit->Solve(std::numeric_limits<double>::infinity()).error ==
            solo_rig::MnaSolveError::NonFinite);

    auto singular_netlist = SupplyCircuit();
    singular_netlist.reference_ground = "0";
    singular_netlist.nodes = {"a", "b", "0"};
    singular_netlist.ports[0].node = "a";
    singular_netlist.supplies[0].positive_node = "a";
    singular_netlist.supplies[0].negative_node = "b";
    singular_netlist.components[0].pins = {"a", "b"};
    auto singular = solo_rig::CompileMnaCircuit(singular_netlist);
    REQUIRE(singular.circuit.has_value());
    REQUIRE(singular.circuit->Prepare(48000.0));
    REQUIRE(singular.circuit->Solve(0.0).error == solo_rig::MnaSolveError::SingularMatrix);
}
