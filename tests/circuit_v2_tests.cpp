#include "content/circuit_v2.hpp"
#include "dsp/mna_solver.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <string_view>

namespace {

constexpr std::string_view kDivider = R"({
  "version":2,
  "schema":"circuit",
  "referenceGround":"0",
  "nodes":["in","out","0"],
  "ports":[
    {"id":"input","direction":"input","node":"in"},
    {"id":"output","direction":"output","node":"out"}
  ],
  "supplies":[],
  "components":[
    {"id":"series","type":"resistor","pins":["in","out"],"value":1000},
    {"id":"load","type":"resistor","pins":["out","0"],"value":1000}
  ],
  "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1,
    "convergence":{"maxIterations":8,"absoluteTolerance":1e-9,"relativeTolerance":1e-6}},
  "futureMetadata":{"owner":"test"}
})";

constexpr std::string_view kRc = R"({
  "version":2,"schema":"circuit","referenceGround":"0",
  "nodes":["in","out","0"],
  "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
  "components":[
    {"id":"r","type":"resistor","pins":["in","out"],"value":1000},
    {"id":"c","type":"capacitor","pins":["out","0"],"value":0.000001,"initial":0}
  ],
  "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
})";

constexpr std::string_view kVoltageSource = R"({
  "version":2,"schema":"circuit","referenceGround":"0","nodes":["out","0"],
  "ports":[{"id":"output","direction":"output","node":"out"}],
  "components":[
    {"id":"source","type":"voltage-source","pins":["out","0"],"value":2},
    {"id":"load","type":"resistor","pins":["out","0"],"value":1000}
  ],"solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
})";

} // namespace

TEST_CASE("circuit v2 parses the versioned contract and tolerates unknown metadata") {
    const auto result = solo_rig::ParseCircuitV2Json(kDivider);
    REQUIRE(result.errors.empty());
    REQUIRE(result.circuit.has_value());
    REQUIRE(result.circuit->version == 2U);
    REQUIRE(result.circuit->reference_ground == "0");
    REQUIRE(result.circuit->ports.size() == 2U);
    REQUIRE(result.circuit->components.size() == 2U);
    REQUIRE(result.circuit->solver.convergence.max_iterations == 8U);
}

TEST_CASE("circuit v2 parses supplies controls and measurement metadata") {
    const auto result = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["a","b","0"],
      "ports":[{"id":"output","direction":"output","node":"a"}],
      "supplies":[{"id":"supply","positive":"a","negative":"0","voltage":9}],
      "components":[
        {"id":"pot","type":"resistor","pins":["a","b"],"value":1000,
          "tolerance":0.05,"potentiometer":{"position":0.5,"minimum":0,"maximum":1}},
        {"id":"mode","type":"switch","pins":["b","0"],"switch":{"states":["clean","clip"],"initial":1}}
      ],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":2},
      "measurementBaseline":{"reference":"bench-1","frequencyHz":1000,"tolerance":0.02}
    })");
    REQUIRE(result.errors.empty());
    REQUIRE(result.circuit.has_value());
    REQUIRE(result.circuit->supplies.size() == 1U);
    REQUIRE(result.circuit->components.at(0).potentiometer.has_value());
    REQUIRE(result.circuit->components.at(1).switch_spec.has_value());
    REQUIRE(result.circuit->solver.oversampling == 2U);
    REQUIRE(result.circuit->measurement_baseline.has_value());
}

TEST_CASE("circuit v2 rejects legacy and unknown versions") {
    const auto legacy = solo_rig::ParseCircuitV2Json(R"({"version":1,"schema":"circuit"})");
    REQUIRE(!legacy.circuit.has_value());
    REQUIRE(!legacy.errors.empty());
    REQUIRE(legacy.errors.front() == solo_rig::CircuitLoadError::UnsupportedVersion);

    const auto unknown = solo_rig::ParseCircuitV2Json(R"({"version":3,"schema":"circuit"})");
    REQUIRE(!unknown.circuit.has_value());
    REQUIRE(unknown.errors.front() == solo_rig::CircuitLoadError::UnsupportedVersion);
}

TEST_CASE("circuit v2 rejects duplicate ids, invalid pins, values, models and paths") {
    const auto duplicate = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["out","0"],
      "ports":[{"id":"output","direction":"output","node":"out"}],
      "components":[
        {"id":"r","type":"resistor","pins":["out","0"],"value":1000},
        {"id":"r","type":"resistor","pins":["out","missing"],"value":1000}
      ],"solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    REQUIRE(duplicate.errors.front() == solo_rig::CircuitLoadError::DuplicateId);

    const auto invalid = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["out","0"],
      "ports":[{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"d","type":"diode","pins":["out","0"],"model":"../silicon","tolerance":-1}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    REQUIRE(!invalid.circuit.has_value());
    REQUIRE(!invalid.errors.empty());
    REQUIRE(invalid.errors.front() == solo_rig::CircuitLoadError::InvalidValue ||
            invalid.errors.front() == solo_rig::CircuitLoadError::UnsafeAssetPath);
}

TEST_CASE("circuit v2 rejects a missing model for a nonlinear component") {
    const auto result = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["out","0"],
      "ports":[{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"d","type":"diode","pins":["out","0"]}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    REQUIRE(!result.circuit.has_value());
    REQUIRE(result.errors.front() == solo_rig::CircuitLoadError::MissingModel);
}

TEST_CASE("circuit v2 rejects unknown components and invalid solver configuration") {
    const auto unknown = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["out","0"],
      "ports":[{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"x","type":"mosfet","pins":["out","0"]}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    REQUIRE(unknown.errors.front() == solo_rig::CircuitLoadError::UnknownComponent);

    const auto solver = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["out","0"],
      "ports":[{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"r","type":"resistor","pins":["out","0"],"value":1000}],
      "solver":{"method":"spice-magic","integration":"forward-euler","oversampling":0,
        "convergence":{"maxIterations":0,"absoluteTolerance":-1,"relativeTolerance":0}}
    })");
    REQUIRE(!solver.circuit.has_value());
    REQUIRE(!solver.errors.empty());
}

TEST_CASE("MNA solves a prepared resistor divider") {
    const auto parsed = solo_rig::ParseCircuitV2Json(kDivider);
    REQUIRE(parsed.circuit.has_value());
    auto compiled = solo_rig::CompileMnaCircuit(*parsed.circuit);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(48000.0));
    const auto result = compiled.circuit->Solve(1.0);
    REQUIRE(result.Succeeded());
    REQUIRE(std::fabs(result.output_voltage - 0.5) < 1.0e-9);
}

TEST_CASE("MNA applies backward Euler capacitor history") {
    const auto parsed = solo_rig::ParseCircuitV2Json(kRc);
    REQUIRE(parsed.circuit.has_value());
    auto compiled = solo_rig::CompileMnaCircuit(*parsed.circuit);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(1000.0));
    const auto first = compiled.circuit->Solve(1.0);
    const auto second = compiled.circuit->Solve(1.0);
    REQUIRE(first.Succeeded());
    REQUIRE(second.Succeeded());
    REQUIRE(std::fabs(first.output_voltage - 0.5) < 1.0e-9);
    REQUIRE(std::fabs(second.output_voltage - 0.75) < 1.0e-9);
}

TEST_CASE("MNA solves independent voltage sources") {
    const auto parsed = solo_rig::ParseCircuitV2Json(kVoltageSource);
    REQUIRE(parsed.circuit.has_value());
    auto compiled = solo_rig::CompileMnaCircuit(*parsed.circuit);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(48000.0));
    const auto result = compiled.circuit->Solve(0.0);
    REQUIRE(result.Succeeded());
    REQUIRE(std::fabs(result.output_voltage - 2.0) < 1.0e-9);
}

TEST_CASE("MNA reports singular matrices and unsupported diode devices") {
    const auto singular_json = R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["a","b","0"],
      "ports":[{"id":"output","direction":"output","node":"a"}],
      "components":[{"id":"r","type":"resistor","pins":["a","b"],"value":1000}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })";
    const auto singular = solo_rig::ParseCircuitV2Json(singular_json);
    REQUIRE(singular.circuit.has_value());
    auto singular_compiled = solo_rig::CompileMnaCircuit(*singular.circuit);
    REQUIRE(singular_compiled.circuit.has_value());
    REQUIRE(singular_compiled.circuit->Prepare(48000.0));
    REQUIRE(singular_compiled.circuit->Solve(0.0).error == solo_rig::MnaSolveError::SingularMatrix);

    const auto diode = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["a","0"],
      "ports":[{"id":"output","direction":"output","node":"a"}],
      "components":[{"id":"d","type":"diode","pins":["a","0"],"model":"silicon"}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    REQUIRE(diode.circuit.has_value());
    const auto diode_compiled = solo_rig::CompileMnaCircuit(*diode.circuit);
    REQUIRE(!diode_compiled.circuit.has_value());
    REQUIRE(diode_compiled.error == solo_rig::MnaCompileError::UnsupportedComponent);
}
