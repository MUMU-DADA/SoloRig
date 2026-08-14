#include "content/circuit_v2.hpp"
#include "dsp/mna_solver.hpp"
#include "content/model_database.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string_view>

namespace {
std::string ReadFixture(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" / "whitebox" / name;
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}

TEST_CASE("M18-C9e dynamic opamp Parse Compile Prepare Solve and Reset preserve BE state") {
    auto parsed = solo_rig::ParseCircuitV2Json(ReadFixture("m18c9e_dynamic_opamp_reference.json"));
    REQUIRE(parsed.circuit.has_value());
    auto compiled = solo_rig::CompileMnaCircuit(*parsed.circuit);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(192000.0));
    const auto first = compiled.circuit->Solve(0.25);
    REQUIRE(first.Succeeded());
    REQUIRE(std::isfinite(first.output_voltage));
    double state = 0.0;
    constexpr double dt = 1.0 / (192000.0 * 4.0);
    constexpr double a = 6.2831853071795864769 * 1000.0 * dt;
    constexpr double factor = a / (1.0 + a);
    for (std::uint32_t substep = 0; substep < 4U; ++substep) {
        state += factor * (10.0 - state);
    }
    const double reference = state * 1000.0 / 1100.0;
    REQUIRE(first.output_voltage > 0.0);
    REQUIRE(std::fabs(first.output_voltage - reference) < 1.0e-9);
    compiled.circuit->Reset();
    const auto second = compiled.circuit->Solve(0.25);
    REQUIRE(second.Succeeded());
    REQUIRE(std::fabs(first.output_voltage - second.output_voltage) < 1.0e-10);
}

TEST_CASE("M18-C9e dynamic opamp rejects nonfinite runtime and exposes nonconvergence") {
    auto parsed = solo_rig::ParseCircuitV2Json(ReadFixture("m18c9e_dynamic_opamp_reference.json"));
    REQUIRE(parsed.circuit.has_value());
    parsed.circuit->solver.convergence.max_iterations = 1U;
    auto compiled = solo_rig::CompileMnaCircuit(*parsed.circuit);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(48000.0));
    REQUIRE(compiled.circuit->Solve(std::numeric_limits<double>::infinity()).error == solo_rig::MnaSolveError::NonFinite);
    REQUIRE(compiled.circuit->Solve(1.0).error == solo_rig::MnaSolveError::NonConvergent);
}

TEST_CASE("M18-C9e dynamic opamp ModelDatabase binding reaches MNA") {
    const auto graph = solo_rig::ParseCircuitV2Json(ReadFixture("m18c9e_dynamic_opamp_audited_reference.json"));
    const auto database = solo_rig::BuildModelDatabase(ReadFixture("m18c9e_cleanroom_dynamic_opamp_model.json"));
    REQUIRE(graph.circuit.has_value());
    REQUIRE(database.database.has_value());
    auto compiled = solo_rig::CompileMnaCircuit(*graph.circuit, *database.database);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(48000.0));
    REQUIRE(compiled.circuit->Solve(0.1).Succeeded());
}
