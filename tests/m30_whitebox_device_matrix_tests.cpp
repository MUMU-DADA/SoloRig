#include "content/circuit_v2.hpp"
#include "dsp/mna_solver.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::string ReadFixture(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" /
                      "whitebox" / name;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open M30 white-box fixture: " + path.string());
    }
    std::ostringstream source;
    source << input.rdbuf();
    return source.str();
}

void RequireDeviceLifecycle(std::string_view fixture_name) {
    auto parsed = solo_rig::ParseCircuitV2Json(ReadFixture(fixture_name));
    REQUIRE(parsed.circuit.has_value());
    parsed.circuit->solver.oversampling = 4U;

    auto compiled = solo_rig::CompileMnaCircuit(*parsed.circuit);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(!compiled.error.has_value());
    for (const double sample_rate : {48000.0, 192000.0}) {
        REQUIRE(compiled.circuit->Prepare(sample_rate));

        const auto first = compiled.circuit->Solve(0.2);
        REQUIRE(first.Succeeded());
        REQUIRE(std::isfinite(first.output_voltage));
        REQUIRE(first.maximum_iteration_count > 0U);

        for (const double input : {-0.2, 0.0, 0.35, 0.55}) {
            const auto result = compiled.circuit->Solve(input);
            if (!result.Succeeded()) {
                throw std::runtime_error("M30 fixture " + std::string(fixture_name) +
                                         " failed at input " + std::to_string(input) +
                                         " with solver error " +
                                         std::to_string(static_cast<int>(result.error)));
            }
            REQUIRE(std::isfinite(result.output_voltage));
            REQUIRE(result.maximum_iteration_count > 0U);
        }

        compiled.circuit->Reset();
        const auto replay = compiled.circuit->Solve(0.2);
        REQUIRE(replay.Succeeded());
        REQUIRE(std::isfinite(replay.output_voltage));
        REQUIRE(std::fabs(replay.output_voltage - first.output_voltage) < 1.0e-8);
    }
    REQUIRE(solo_rig::IsRealtimeSafe(compiled.circuit->ProcessContract()));
}

} // namespace

TEST_CASE("M30 white-box device matrix closes Compile Prepare Solve Reset for diode BJT JFET MOSFET and opamp") {
    RequireDeviceLifecycle("m18c1_diode_dynamic.json");
    RequireDeviceLifecycle("m18c2_bjt_inline_npn.json");
    RequireDeviceLifecycle("m18c3_jfet_inline_n.json");
    RequireDeviceLifecycle("m18c4_mosfet_inline_n.json");
    RequireDeviceLifecycle("m18c5_opamp_inline.json");
}

TEST_CASE("M30 white-box matrix keeps nonfinite and invalid preparation boundaries explicit") {
    const auto parsed = solo_rig::ParseCircuitV2Json(ReadFixture("m18c2_bjt_inline_npn.json"));
    REQUIRE(parsed.circuit.has_value());
    auto compiled = solo_rig::CompileMnaCircuit(*parsed.circuit);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(!compiled.circuit->Prepare(0.0));
    REQUIRE(compiled.circuit->Prepare(192000.0));
    REQUIRE(compiled.circuit->Solve(std::numeric_limits<double>::quiet_NaN()).error ==
            solo_rig::MnaSolveError::NonFinite);

    const auto singular = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["out","0"],
      "ports":[{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"d","type":"diode","pins":["out","0"],"model":{
        "kind":"shockley","saturationCurrent":1e-30,"thermalVoltage":1.0,
        "maxStep":1.0,"junctionCapacitance":0.0,"junctionPotential":0.7,
        "gradingCoefficient":0.5,"temperatureCelsius":25.0,
        "leakageCurrent":0.0,"leakageTemperatureCoefficient":0.0}}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    REQUIRE(singular.circuit.has_value());
    auto singular_compiled = solo_rig::CompileMnaCircuit(*singular.circuit);
    REQUIRE(singular_compiled.circuit.has_value());
    REQUIRE(singular_compiled.circuit->Prepare(48000.0));
    REQUIRE(singular_compiled.circuit->Solve(0.0).error ==
            solo_rig::MnaSolveError::SingularMatrix);
}
