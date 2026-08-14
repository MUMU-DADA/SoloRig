#include "content/circuit_v2.hpp"
#include "content/model_database.hpp"
#include "dsp/mna_solver.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {

std::string ReadFixture(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" / "whitebox" / name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open M18-C9a fixture: " + path.string());
    std::ostringstream source;
    source << stream.rdbuf();
    return source.str();
}

double IndependentStep(double input, double history, double sample_rate) {
    constexpr double resistance = 1000.0;
    constexpr double saturation_current = 1.0e-12;
    constexpr double leakage_current = 1.0e-12;
    constexpr double thermal_voltage = 0.02585;
    constexpr double capacitance = 1.0e-6;
    double voltage = history;
    for (int iteration = 0; iteration < 64; ++iteration) {
        const double forward = std::exp(voltage / thermal_voltage);
        const double reverse = std::exp(-voltage / thermal_voltage);
        const double static_current = saturation_current * (forward - 1.0) -
            leakage_current * (reverse - 1.0);
        const double residual = (voltage - input) / resistance + static_current +
            capacitance * sample_rate * (voltage - history);
        const double jacobian = 1.0 / resistance + saturation_current / thermal_voltage * forward +
            leakage_current / thermal_voltage * reverse + capacitance * sample_rate;
        const double next = voltage - residual / jacobian;
        if (std::fabs(next - voltage) < 1.0e-14) return next;
        voltage = next;
    }
    return voltage;
}

} // namespace

TEST_CASE("M18-C9a audited diode matches independent backward-Euler reference and Reset") {
    const auto database = solo_rig::BuildModelDatabase(ReadFixture("m18c9a_cleanroom_diode_model.json"));
    const auto parsed = solo_rig::ParseCircuitV2Json(ReadFixture("m18c9a_diode_dynamic_reference.json"));
    REQUIRE(database.database.has_value());
    REQUIRE(parsed.circuit.has_value());
    auto compiled = solo_rig::CompileMnaCircuit(*parsed.circuit, *database.database);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(1000.0));
    double history = 0.1;
    for (const double input : {1.0, 1.0, -0.25, 0.5}) {
        const double reference = IndependentStep(input, history, 1000.0);
        const auto solved = compiled.circuit->Solve(input);
        REQUIRE(solved.Succeeded());
        REQUIRE(std::fabs(solved.output_voltage - reference) < 1.0e-9);
        history = reference;
    }
    compiled.circuit->Reset();
    const auto reset = compiled.circuit->Solve(1.0);
    REQUIRE(reset.Succeeded());
    REQUIRE(std::fabs(reset.output_voltage - IndependentStep(1.0, 0.1, 1000.0)) < 1.0e-9);
}

TEST_CASE("M18-C9a inline temperature leakage and capacitance fields change dynamic output") {
    const auto source = [](double temperature, double leakage, double capacitance) {
        std::ostringstream json;
        json << R"({"version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
          "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
          "components":[{"id":"r","type":"resistor","pins":["in","out"],"value":1000},
          {"id":"d","type":"diode","pins":["out","0"],"initial":0.1,"model":{"kind":"shockley",
          "saturationCurrent":1e-12,"thermalVoltage":0.02585,"maxStep":1,"junctionCapacitance":)"
             << capacitance << R"(,"junctionPotential":0.7,"gradingCoefficient":0.5,"temperatureCelsius":)"
             << temperature << R"(,"leakageCurrent":)" << leakage
             << R"(,"leakageTemperatureCoefficient":0.01}}],
          "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1,
          "convergence":{"maxIterations":64,"absoluteTolerance":1e-12,"relativeTolerance":1e-10}}})";
        return json.str();
    };
    const auto cold_parsed = solo_rig::ParseCircuitV2Json(source(25.0, 0.0, 0.0));
    const auto hot_parsed = solo_rig::ParseCircuitV2Json(source(125.0, 1.0e-6, 1.0e-6));
    REQUIRE(cold_parsed.circuit.has_value());
    REQUIRE(hot_parsed.circuit.has_value());
    auto cold = solo_rig::CompileMnaCircuit(*cold_parsed.circuit);
    auto hot = solo_rig::CompileMnaCircuit(*hot_parsed.circuit);
    REQUIRE(cold.circuit->Prepare(48000.0));
    REQUIRE(hot.circuit->Prepare(48000.0));
    const auto cold_result = cold.circuit->Solve(0.5);
    const auto hot_result = hot.circuit->Solve(0.5);
    REQUIRE(cold_result.Succeeded());
    REQUIRE(hot_result.Succeeded());
    REQUIRE(std::fabs(cold_result.output_voltage - hot_result.output_voltage) > 1.0e-6);
}

TEST_CASE("M18-C9a audited Zener remains finite in breakdown at 192 kHz four-times oversampling") {
    const auto database = solo_rig::BuildModelDatabase(ReadFixture("m18c9a_cleanroom_zener_model.json"));
    REQUIRE(database.database.has_value());
    const auto parsed = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"r","type":"resistor","pins":["in","out"],"value":10000},
        {"id":"z","type":"diode","pins":["out","0"],"model":"solorig.cleanroom.zener.dynamic-junction.v1"}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4,
        "convergence":{"maxIterations":64,"absoluteTolerance":1e-10,"relativeTolerance":1e-8}}
    })");
    REQUIRE(parsed.circuit.has_value());
    auto compiled = solo_rig::CompileMnaCircuit(*parsed.circuit, *database.database);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(192000.0));
    for (int sample = 0; sample < 32; ++sample) {
        const auto result = compiled.circuit->Solve(-10.0);
        REQUIRE(result.Succeeded());
        REQUIRE(std::isfinite(result.output_voltage));
    }
    REQUIRE(solo_rig::IsRealtimeSafe(compiled.circuit->ProcessContract()));
}

TEST_CASE("M18-C9a parser and MNA reject invalid nonfinite nonconvergent and singular boundaries") {
    const auto invalid = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["out","0"],
      "ports":[{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"d","type":"diode","pins":["out","0"],"model":{"kind":"shockley",
        "junctionCapacitance":-1e-9,"temperatureCelsius":25}}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}})");
    REQUIRE(!invalid.circuit.has_value());
    REQUIRE(invalid.errors.front() == solo_rig::CircuitLoadError::InvalidValue);

    const auto difficult = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"r","type":"resistor","pins":["in","out"],"value":1000},
        {"id":"d","type":"diode","pins":["out","0"],"model":{"kind":"shockley","maxStep":0.001,
          "junctionCapacitance":1e-9,"junctionPotential":0.7,"gradingCoefficient":0.5,
          "temperatureCelsius":25,"leakageCurrent":0,"leakageTemperatureCoefficient":0}}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1,
        "convergence":{"maxIterations":1,"absoluteTolerance":1e-12,"relativeTolerance":1e-12}}})");
    auto nonconvergent = solo_rig::CompileMnaCircuit(*difficult.circuit);
    REQUIRE(nonconvergent.circuit->Prepare(48000.0));
    REQUIRE(nonconvergent.circuit->Solve(10.0).error == solo_rig::MnaSolveError::NonConvergent);
    REQUIRE(nonconvergent.circuit->Solve(std::numeric_limits<double>::infinity()).error ==
            solo_rig::MnaSolveError::NonFinite);

    const auto singular_source = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["out","0"],
      "ports":[{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"d","type":"diode","pins":["out","0"],"model":{"kind":"shockley",
        "saturationCurrent":1e-30,"thermalVoltage":1,"maxStep":1,"junctionCapacitance":0,
        "junctionPotential":0.7,"gradingCoefficient":0.5,"temperatureCelsius":25,
        "leakageCurrent":0,"leakageTemperatureCoefficient":0}}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}})");
    auto singular = solo_rig::CompileMnaCircuit(*singular_source.circuit);
    REQUIRE(singular.circuit->Prepare(48000.0));
    REQUIRE(singular.circuit->Solve(0.0).error == solo_rig::MnaSolveError::SingularMatrix);
}
