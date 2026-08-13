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
    if (!stream) {
        throw std::runtime_error("cannot open M18-C1 fixture: " + path.string());
    }
    std::ostringstream source;
    source << stream.rdbuf();
    return source.str();
}

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

TEST_CASE("circuit v2 parses parameterized potentiometer endpoints and wiper mapping") {
    const auto result = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0",
      "nodes":["in","wiper","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"wiper"}],
      "components":[{"id":"p","type":"potentiometer","pins":["in","0","wiper"],"value":10000,
        "potentiometer":{"position":0.25,"minimum":0,"maximum":1,"wiperNode":"wiper"}}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    REQUIRE(result.errors.empty());
    REQUIRE(result.circuit.has_value());
    REQUIRE(result.circuit->components.front().value == 10000.0);
    REQUIRE(result.circuit->components.front().potentiometer.has_value());
    REQUIRE(result.circuit->components.front().potentiometer->position == 0.25);
    REQUIRE(result.circuit->components.front().potentiometer->wiper_node == "wiper");

    const auto invalid_range = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","wiper","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"wiper"}],
      "components":[{"id":"p","type":"potentiometer","pins":["in","0","wiper"],"value":10000,
        "potentiometer":{"position":1.1,"minimum":0,"maximum":1,"wiperNode":"wiper"}}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    REQUIRE(!invalid_range.circuit.has_value());
    REQUIRE(invalid_range.errors.front() == solo_rig::CircuitLoadError::InvalidValue);

    const auto invalid_wiper = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","wiper","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"wiper"}],
      "components":[{"id":"p","type":"potentiometer","pins":["in","0","wiper"],"value":10000,
        "potentiometer":{"position":0.5,"minimum":0,"maximum":1,"wiperNode":"missing"}}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    REQUIRE(!invalid_wiper.circuit.has_value());
    REQUIRE(invalid_wiper.errors.front() == solo_rig::CircuitLoadError::InvalidNode ||
            invalid_wiper.errors.front() == solo_rig::CircuitLoadError::InvalidPin);

    const auto invalid_total = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","wiper","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"wiper"}],
      "components":[{"id":"p","type":"potentiometer","pins":["in","0","wiper"],"value":1e309,
        "potentiometer":{"position":0.5,"minimum":0,"maximum":1,"wiperNode":"wiper"}}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    REQUIRE(!invalid_total.circuit.has_value());
    REQUIRE(invalid_total.errors.front() == solo_rig::CircuitLoadError::InvalidJson ||
            invalid_total.errors.front() == solo_rig::CircuitLoadError::InvalidValue);
}

TEST_CASE("circuit v2 parses VCCS and ideal op amp contracts and rejects invalid controls") {
    const auto result = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["out","cp","cn","0"],
      "ports":[{"id":"output","direction":"output","node":"out"}],
      "components":[
        {"id":"g","type":"vccs","pins":["out","0","cp","cn"],
         "controlledSource":{"kind":"vccs","modelVersion":1,"gain":0.001,"minimum":-1,"maximum":1}},
        {"id":"a","type":"ideal-op-amp","pins":["out","0","cp","cn"],
         "controlledSource":{"kind":"ideal-op-amp","modelVersion":1,"gain":100000,"minimum":-12,"maximum":12}}
      ],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    REQUIRE(result.errors.empty());
    REQUIRE(result.circuit.has_value());
    REQUIRE(result.circuit->components.at(0).controlled_source->kind == "vccs");
    REQUIRE(result.circuit->components.at(0).controlled_source->gain == 0.001);
    REQUIRE(result.circuit->components.at(1).controlled_source->kind == "ideal-op-amp");
    REQUIRE(result.circuit->components.at(1).controlled_source->maximum == 12.0);

    const auto bad_kind = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["out","cp","cn","0"],
      "ports":[{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"g","type":"vccs","pins":["out","0","cp","cn"],
        "controlledSource":{"kind":"ideal-op-amp","modelVersion":1,"gain":1}}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    REQUIRE(!bad_kind.circuit.has_value());
    REQUIRE(bad_kind.errors.front() == solo_rig::CircuitLoadError::InvalidValue);

    const auto bad_range = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["out","cp","cn","0"],
      "ports":[{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"g","type":"vccs","pins":["out","0","cp","cn"],
        "controlledSource":{"kind":"vccs","modelVersion":2,"gain":1,"minimum":1,"maximum":-1}}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    REQUIRE(!bad_range.circuit.has_value());
    REQUIRE(bad_range.errors.front() == solo_rig::CircuitLoadError::InvalidValue);
}

TEST_CASE("MNA solves VCCS and ideal op amp inverting and non-inverting feedback networks") {
    const auto make_vccs = [] {
        solo_rig::CircuitNetlist circuit{
            .version = 2,
            .schema = "circuit",
            .reference_ground = "0",
            .nodes = {"in", "out", "0"},
            .ports = {{.id = "input", .direction = solo_rig::CircuitPortDirection::Input, .node = "in"},
                      {.id = "output", .direction = solo_rig::CircuitPortDirection::Output, .node = "out"}},
            .components = {{.id = "g",
                            .type = solo_rig::CircuitComponentType::Vccs,
                            .pins = {"out", "0", "in", "0"},
                            .controlled_source = solo_rig::CircuitControlledSourceSpec{.kind = "vccs",
                                                                                          .gain = 0.001}},
                           {.id = "load",
                            .type = solo_rig::CircuitComponentType::Resistor,
                            .pins = {"out", "0"},
                            .value = 1000.0}},
            .solver = {}};
        return circuit;
    };
    auto vccs = solo_rig::CompileMnaCircuit(make_vccs());
    REQUIRE(vccs.circuit.has_value());
    REQUIRE(vccs.circuit->Prepare(48000.0));
    REQUIRE(std::fabs(vccs.circuit->Solve(1.0).output_voltage - 1.0) < 1.0e-6);

    const auto make_opamp = [](bool inverting) {
        solo_rig::CircuitNetlist circuit{
            .version = 2,
            .schema = "circuit",
            .reference_ground = "0",
            .nodes = {"in", "out", "fb", "0"},
            .ports = {{.id = "input", .direction = solo_rig::CircuitPortDirection::Input, .node = "in"},
                      {.id = "output", .direction = solo_rig::CircuitPortDirection::Output, .node = "out"}},
            .components = {{.id = "a",
                            .type = solo_rig::CircuitComponentType::IdealOpAmp,
                            .pins = {"out", "0", inverting ? "0" : "in", "fb"},
                            .controlled_source = solo_rig::CircuitControlledSourceSpec{.kind = "ideal-op-amp",
                                                                                          .gain = 100000.0}},
                           {.id = "feedback",
                            .type = solo_rig::CircuitComponentType::Resistor,
                            .pins = {"out", "fb"},
                            .value = 10000.0},
                           {.id = "input-network",
                            .type = solo_rig::CircuitComponentType::Resistor,
                            .pins = {inverting ? "in" : "fb", inverting ? "fb" : "0"},
                            .value = 10000.0},
                           {.id = "bias",
                            .type = solo_rig::CircuitComponentType::Resistor,
                            .pins = {"fb", "0"},
                            .value = 10000.0}},
            .solver = {}};
        if (!inverting) {
            circuit.components.pop_back();
        }
        return circuit;
    };
    auto non_inverting = solo_rig::CompileMnaCircuit(make_opamp(false));
    REQUIRE(non_inverting.circuit.has_value());
    REQUIRE(non_inverting.circuit->Prepare(48000.0));
    REQUIRE(std::fabs(non_inverting.circuit->Solve(1.0).output_voltage - 2.0) < 5.0e-4);
    auto inverting = solo_rig::CompileMnaCircuit(make_opamp(true));
    REQUIRE(inverting.circuit.has_value());
    REQUIRE(inverting.circuit->Prepare(48000.0));
    REQUIRE(std::fabs(inverting.circuit->Solve(1.0).output_voltage + 1.0) < 5.0e-4);
}

TEST_CASE("MNA applies ideal op amp output limits and reports controlled-source singularity") {
    solo_rig::CircuitNetlist limited{
        .version = 2,
        .schema = "circuit",
        .reference_ground = "0",
        .nodes = {"in", "out", "0"},
        .ports = {{.id = "input", .direction = solo_rig::CircuitPortDirection::Input, .node = "in"},
                  {.id = "output", .direction = solo_rig::CircuitPortDirection::Output, .node = "out"}},
        .components = {{.id = "a",
                        .type = solo_rig::CircuitComponentType::IdealOpAmp,
                        .pins = {"out", "0", "in", "out"},
                        .controlled_source = solo_rig::CircuitControlledSourceSpec{.kind = "ideal-op-amp",
                                                                                      .gain = 100.0,
                                                                                      .minimum = -0.5,
                                                                                      .maximum = 0.5},
                        }},
        .solver = {}};
    auto compiled = solo_rig::CompileMnaCircuit(limited);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(48000.0));
    const auto limited_output = compiled.circuit->Solve(2.0);
    REQUIRE(limited_output.Succeeded());
    REQUIRE(std::fabs(limited_output.output_voltage - 0.5) < 1.0e-6);

    limited.components.front().controlled_source->gain = std::numeric_limits<double>::infinity();
    const auto invalid = solo_rig::CompileMnaCircuit(limited);
    REQUIRE(!invalid.circuit.has_value());
    REQUIRE(invalid.error == solo_rig::MnaCompileError::InvalidValue);

    solo_rig::CircuitNetlist singular = limited;
    singular.components.front().type = solo_rig::CircuitComponentType::Vccs;
    singular.components.front().pins = {"out", "0", "in", "0"};
    singular.components.front().controlled_source = solo_rig::CircuitControlledSourceSpec{.kind = "vccs", .gain = 1.0};
    auto singular_compiled = solo_rig::CompileMnaCircuit(singular);
    REQUIRE(singular_compiled.circuit.has_value());
    REQUIRE(singular_compiled.circuit->Prepare(48000.0));
    REQUIRE(singular_compiled.circuit->Solve(1.0).error == solo_rig::MnaSolveError::SingularMatrix);
}

TEST_CASE("circuit v2 parses BJT Ebers-Moll fields and rejects unsafe model ranges") {
    const auto result = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["c","b","e","0"],
      "ports":[{"id":"output","direction":"output","node":"c"}],
      "components":[{"id":"q1","type":"bjt","pins":["c","b","e"],
        "bjtModel":{"kind":"ebers-moll","modelVersion":1,"polarity":"npn",
          "saturationCurrent":1e-15,"forwardBeta":100,"reverseBeta":1,"thermalVoltage":0.02585,
          "maxCurrent":0.01,"maxStep":1}}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    REQUIRE(result.errors.empty());
    REQUIRE(result.circuit.has_value());
    REQUIRE(result.circuit->components.front().type == solo_rig::CircuitComponentType::Bjt);
    REQUIRE(result.circuit->components.front().bjt_model->polarity == "npn");
    REQUIRE(result.circuit->components.front().bjt_model->forward_beta == 100.0);

    const auto invalid = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["c","b","e","0"],
      "ports":[{"id":"output","direction":"output","node":"c"}],
      "components":[{"id":"q1","type":"bjt","pins":["c","b","e"],
        "bjtModel":{"kind":"ebers-moll","modelVersion":2,"polarity":"sideways",
          "saturationCurrent":-1,"forwardBeta":0,"reverseBeta":1,"thermalVoltage":0,
          "maxCurrent":0,"maxStep":0}}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    REQUIRE(!invalid.circuit.has_value());
    REQUIRE(invalid.errors.front() == solo_rig::CircuitLoadError::InvalidValue);

    const auto ambiguous = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["c","b","e","0"],
      "ports":[{"id":"output","direction":"output","node":"c"}],
      "components":[{"id":"q1","type":"bjt","pins":["c","b","e"],"value":1,
        "bjtModel":{"kind":"ebers-moll","modelVersion":1,"polarity":"pnp",
          "saturationCurrent":1e-15,"forwardBeta":100,"reverseBeta":1,"thermalVoltage":0.02585,
          "maxCurrent":0.01,"maxStep":1}}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    REQUIRE(!ambiguous.circuit.has_value());
    REQUIRE(ambiguous.errors.front() == solo_rig::CircuitLoadError::InvalidValue);
}

TEST_CASE("MNA solves BJT common-emitter cutoff active and saturated bias points") {
    const auto make_circuit = [] {
        solo_rig::CircuitNetlist circuit{
            .version = 2,
            .schema = "circuit",
            .reference_ground = "0",
            .nodes = {"collector", "base", "vcc", "0"},
            .ports = {{.id = "input", .direction = solo_rig::CircuitPortDirection::Input, .node = "base"},
                      {.id = "output", .direction = solo_rig::CircuitPortDirection::Output, .node = "collector"}},
            .supplies = {{.id = "vcc", .positive_node = "vcc", .negative_node = "0", .voltage = 5.0}},
            .components = {{.id = "collector-load",
                            .type = solo_rig::CircuitComponentType::Resistor,
                            .pins = {"vcc", "collector"},
                            .value = 1000.0},
                           {.id = "q1",
                            .type = solo_rig::CircuitComponentType::Bjt,
                            .pins = {"collector", "base", "0"},
                            .initial = 0.0,
                            .bjt_model = solo_rig::CircuitBjtModel{.kind = "ebers-moll",
                                                                     .model_version = 1,
                                                                     .polarity = "npn",
                                                                     .saturation_current = 1.0e-15,
                                                                     .forward_beta = 100.0,
                                                                     .reverse_beta = 1.0,
                                                                     .thermal_voltage = 0.02585,
                                                                     .max_current = 0.005,
                                                                     .max_step = 10.0}}},
            .solver = {.method = "modified-nodal-analysis",
                       .integration = "backward-euler",
                       .oversampling = 1,
                       .convergence = {.max_iterations = 64,
                                       .absolute_tolerance = 1.0e-9,
                                       .relative_tolerance = 1.0e-6}}};
        return circuit;
    };
    auto solve = [&](double base_voltage) {
        auto circuit = make_circuit();
        auto compiled = solo_rig::CompileMnaCircuit(circuit);
        REQUIRE(compiled.circuit.has_value());
        REQUIRE(compiled.circuit->Prepare(48000.0));
        const auto first = compiled.circuit->Solve(base_voltage);
        REQUIRE(first.Succeeded());
        compiled.circuit->Reset();
        const auto reset = compiled.circuit->Solve(base_voltage);
        REQUIRE(reset.Succeeded());
        REQUIRE(std::fabs(reset.output_voltage - first.output_voltage) < 1.0e-8);
        REQUIRE(compiled.circuit->Prepare(48000.0));
        const auto prepared_again = compiled.circuit->Solve(base_voltage);
        REQUIRE(prepared_again.Succeeded());
        REQUIRE(std::fabs(prepared_again.output_voltage - first.output_voltage) < 1.0e-8);
        return first.output_voltage;
    };
    const auto cutoff = solve(0.0);
    const auto active = solve(0.6);
    const auto saturated = solve(0.8);
    REQUIRE(cutoff > active);
    REQUIRE(active > saturated);
    REQUIRE(cutoff > 4.9);
    REQUIRE(saturated < 0.5);
    for (const auto base_voltage : {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8}) {
        REQUIRE(std::isfinite(solve(base_voltage)));
    }
}

TEST_CASE("MNA BJT PNP response mirrors the NPN common-emitter reference") {
    const auto solve = [](std::string polarity, double supply_voltage, double base_voltage) {
        solo_rig::CircuitNetlist circuit{
            .version = 2,
            .schema = "circuit",
            .reference_ground = "0",
            .nodes = {"collector", "base", "rail", "0"},
            .ports = {{.id = "input", .direction = solo_rig::CircuitPortDirection::Input, .node = "base"},
                      {.id = "output", .direction = solo_rig::CircuitPortDirection::Output, .node = "collector"}},
            .supplies = {{.id = "rail", .positive_node = "rail", .negative_node = "0", .voltage = supply_voltage}},
            .components = {{.id = "collector-load",
                            .type = solo_rig::CircuitComponentType::Resistor,
                            .pins = {"rail", "collector"},
                            .value = 1000.0},
                           {.id = "q1",
                            .type = solo_rig::CircuitComponentType::Bjt,
                            .pins = {"collector", "base", "0"},
                            .bjt_model = solo_rig::CircuitBjtModel{.kind = "ebers-moll",
                                                                     .model_version = 1,
                                                                     .polarity = std::move(polarity),
                                                                     .saturation_current = 1.0e-15,
                                                                     .forward_beta = 100.0,
                                                                     .reverse_beta = 1.0,
                                                                     .thermal_voltage = 0.02585,
                                                                     .max_current = 0.005,
                                                                     .max_step = 10.0}}},
            .solver = {.method = "modified-nodal-analysis",
                       .integration = "backward-euler",
                       .oversampling = 1,
                       .convergence = {.max_iterations = 64,
                                       .absolute_tolerance = 1.0e-9,
                                       .relative_tolerance = 1.0e-6}}};
        auto compiled = solo_rig::CompileMnaCircuit(circuit);
        REQUIRE(compiled.circuit.has_value());
        REQUIRE(compiled.circuit->Prepare(48000.0));
        const auto result = compiled.circuit->Solve(base_voltage);
        REQUIRE(result.Succeeded());
        return result.output_voltage;
    };
    for (const auto magnitude : {0.0, 0.4, 0.6, 0.8}) {
        const auto npn = solve("npn", 5.0, magnitude);
        const auto pnp = solve("pnp", -5.0, -magnitude);
        REQUIRE(std::fabs(npn + pnp) < 1.0e-8);
    }
}

TEST_CASE("MNA BJT rejects non-finite, non-convergent and singular configurations") {
    solo_rig::CircuitNetlist circuit{
        .version = 2,
        .schema = "circuit",
        .reference_ground = "0",
        .nodes = {"collector", "base", "0"},
        .ports = {{.id = "input", .direction = solo_rig::CircuitPortDirection::Input, .node = "base"},
                  {.id = "output", .direction = solo_rig::CircuitPortDirection::Output, .node = "collector"}},
        .components = {{.id = "q1",
                        .type = solo_rig::CircuitComponentType::Bjt,
                        .pins = {"collector", "base", "0"},
                        .bjt_model = solo_rig::CircuitBjtModel{.kind = "ebers-moll",
                                                                 .polarity = "npn",
                                                                 .saturation_current = 1.0e-15,
                                                                 .forward_beta = 100.0,
                                                                 .reverse_beta = 1.0,
                                                                 .thermal_voltage = 0.02585,
                                                                 .max_current = 0.005,
                                                                 .max_step = 0.5}},
                       {.id = "collector-load",
                        .type = solo_rig::CircuitComponentType::Resistor,
                        .pins = {"collector", "0"},
                        .value = 1000.0}},
        .solver = {.method = "modified-nodal-analysis",
                   .integration = "backward-euler",
                   .oversampling = 1,
                   .convergence = {.max_iterations = 1,
                                   .absolute_tolerance = 1.0e-14,
                                   .relative_tolerance = 1.0e-14}}};
    circuit.components.front().bjt_model->saturation_current = std::numeric_limits<double>::infinity();
    const auto invalid = solo_rig::CompileMnaCircuit(circuit);
    REQUIRE(!invalid.circuit.has_value());
    REQUIRE(invalid.error == solo_rig::MnaCompileError::InvalidValue);

    circuit.components.front().bjt_model->saturation_current = 1.0e-15;
    circuit.components.front().bjt_model->max_step = 1.0e-20;
    auto nonconvergent = solo_rig::CompileMnaCircuit(circuit);
    REQUIRE(nonconvergent.circuit.has_value());
    REQUIRE(nonconvergent.circuit->Prepare(48000.0));
    const auto nonconvergent_result = nonconvergent.circuit->Solve(2.0);
    REQUIRE(nonconvergent_result.error == solo_rig::MnaSolveError::NonConvergent);

    circuit.components.front().bjt_model->max_step = 1.0;
    circuit.solver.convergence.max_iterations = 64;
    circuit.components.pop_back();
    auto singular = solo_rig::CompileMnaCircuit(circuit);
    REQUIRE(singular.circuit.has_value());
    REQUIRE(singular.circuit->Prepare(48000.0));
    REQUIRE(singular.circuit->Solve(0.5).error == solo_rig::MnaSolveError::SingularMatrix);
}

TEST_CASE("MNA compiles a static potentiometer network and sweeps its prepared position") {
    const auto make_circuit = [](double position) {
        solo_rig::CircuitNetlist circuit{
            .version = 2,
            .schema = "circuit",
            .reference_ground = "0",
            .nodes = {"in", "wiper", "0"},
            .ports = {{.id = "input", .direction = solo_rig::CircuitPortDirection::Input, .node = "in"},
                      {.id = "output", .direction = solo_rig::CircuitPortDirection::Output, .node = "wiper"}},
            .components = {{.id = "p",
                            .type = solo_rig::CircuitComponentType::Potentiometer,
                            .pins = {"in", "0", "wiper"},
                            .value = 10000.0,
                            .potentiometer = solo_rig::CircuitPotentiometerSpec{.position = position,
                                                                                 .minimum = 0.0,
                                                                                 .maximum = 1.0,
                                                                                 .wiper_node = "wiper"}}},
            .solver = {}};
        return circuit;
    };
    for (const auto position : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        auto compiled = solo_rig::CompileMnaCircuit(make_circuit(position));
        REQUIRE(compiled.circuit.has_value());
        REQUIRE(compiled.circuit->Prepare(48000.0));
        const auto first = compiled.circuit->Solve(1.0);
        REQUIRE(first.Succeeded());
        REQUIRE(std::fabs(first.output_voltage - (1.0 - position)) < 1.0e-6);
        compiled.circuit->Reset();
        const auto reset = compiled.circuit->Solve(1.0);
        REQUIRE(reset.Succeeded());
        REQUIRE(std::fabs(reset.output_voltage - first.output_voltage) < 1.0e-9);
        REQUIRE(compiled.circuit->Prepare(48000.0));
        const auto prepared_again = compiled.circuit->Solve(1.0);
        REQUIRE(prepared_again.Succeeded());
        REQUIRE(std::fabs(prepared_again.output_voltage - first.output_voltage) < 1.0e-9);
    }
}

TEST_CASE("MNA rejects a potentiometer without a wiper and non-finite total resistance") {
    solo_rig::CircuitNetlist no_wiper{
        .version = 2,
        .schema = "circuit",
        .reference_ground = "0",
        .nodes = {"in", "0"},
        .ports = {{.id = "input", .direction = solo_rig::CircuitPortDirection::Input, .node = "in"},
                  {.id = "output", .direction = solo_rig::CircuitPortDirection::Output, .node = "in"}},
        .components = {{.id = "p",
                        .type = solo_rig::CircuitComponentType::Potentiometer,
                        .pins = {"in", "0"},
                        .value = 10000.0,
                        .potentiometer = solo_rig::CircuitPotentiometerSpec{}}},
        .solver = {}};
    const auto rejected = solo_rig::CompileMnaCircuit(no_wiper);
    REQUIRE(!rejected.circuit.has_value());
    REQUIRE(rejected.error == solo_rig::MnaCompileError::UnsupportedComponent);

    no_wiper.components.front().value = std::numeric_limits<double>::infinity();
    const auto invalid = solo_rig::CompileMnaCircuit(no_wiper);
    REQUIRE(!invalid.circuit.has_value());
    REQUIRE(invalid.error == solo_rig::MnaCompileError::InvalidValue);
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

TEST_CASE("MNA reports singular matrices and supports the compatible silicon diode model") {
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
    auto diode_compiled = solo_rig::CompileMnaCircuit(*diode.circuit);
    REQUIRE(diode_compiled.circuit.has_value());
    REQUIRE(diode_compiled.circuit->Prepare(48000.0));
    const auto first = diode_compiled.circuit->Solve(1.0);
    const auto second = diode_compiled.circuit->Solve(1.0);
    REQUIRE(first.Succeeded());
    REQUIRE(second.Succeeded());
    REQUIRE(std::isfinite(first.output_voltage));
    REQUIRE(first.output_voltage == second.output_voltage);
}

TEST_CASE("circuit v2 parses diode model parameters and rejects unsafe or non-finite values") {
    constexpr std::string_view valid = R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
      "components":[
        {"id":"r","type":"resistor","pins":["in","out"],"value":1000},
        {"id":"d","type":"diode","pins":["out","0"],"initial":0.2,
         "model":{"kind":"shockley","saturationCurrent":1e-9,"thermalVoltage":0.02585,"maxStep":0.5}}
      ],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1,
        "convergence":{"maxIterations":12,"absoluteTolerance":1e-10,"relativeTolerance":1e-7}}
    })";
    const auto parsed = solo_rig::ParseCircuitV2Json(valid);
    REQUIRE(parsed.circuit.has_value());
    REQUIRE(parsed.circuit->components.at(1).diode_model.has_value());
    REQUIRE(parsed.circuit->components.at(1).diode_model->max_step == 0.5);
    const auto invalid = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["out","0"],
      "ports":[{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"d","type":"diode","pins":["out","0"],
        "model":{"kind":"shockley","saturationCurrent":0,"thermalVoltage":-1,"maxStep":0}}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1,
        "convergence":{"maxIterations":0,"absoluteTolerance":0,"relativeTolerance":0}}
    })");
    REQUIRE(!invalid.circuit.has_value());
    REQUIRE(!invalid.errors.empty());
}

TEST_CASE("MNA diode Newton produces a bounded deterministic operating point") {
    constexpr std::string_view source = R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
      "components":[
        {"id":"r","type":"resistor","pins":["in","out"],"value":1000},
        {"id":"d","type":"diode","pins":["out","0"],"initial":0.1,
         "model":{"kind":"shockley","saturationCurrent":1e-9,"thermalVoltage":0.02585,"maxStep":1}}
      ],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1,
        "convergence":{"maxIterations":32,"absoluteTolerance":1e-9,"relativeTolerance":1e-6}}
    })";
    const auto parsed = solo_rig::ParseCircuitV2Json(source);
    REQUIRE(parsed.circuit.has_value());
    auto compiled = solo_rig::CompileMnaCircuit(*parsed.circuit);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(48000.0));
    const auto first = compiled.circuit->Solve(0.5);
    const auto second = compiled.circuit->Solve(0.5);
    REQUIRE(first.Succeeded());
    REQUIRE(second.Succeeded());
    REQUIRE(first.output_voltage > 0.0);
    REQUIRE(first.output_voltage < 0.5);
    REQUIRE(std::fabs(first.output_voltage - second.output_voltage) < 1.0e-6);
}

TEST_CASE("MNA diode Newton reports deterministic non-convergence, non-finite input and bounded iterations") {
    constexpr std::string_view source = R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
      "components":[
        {"id":"r","type":"resistor","pins":["in","out"],"value":1000},
        {"id":"d","type":"diode","pins":["out","0"],"model":{"kind":"shockley","saturationCurrent":1e-12,"thermalVoltage":0.02585,"maxStep":0.001}}
      ],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1,
        "convergence":{"maxIterations":1,"absoluteTolerance":1e-14,"relativeTolerance":1e-14}}
    })";
    const auto parsed = solo_rig::ParseCircuitV2Json(source);
    REQUIRE(parsed.circuit.has_value());
    auto compiled = solo_rig::CompileMnaCircuit(*parsed.circuit);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(48000.0));
    REQUIRE(compiled.circuit->Solve(std::numeric_limits<double>::quiet_NaN()).error == solo_rig::MnaSolveError::NonFinite);
    REQUIRE(compiled.circuit->Solve(10.0).error == solo_rig::MnaSolveError::NonConvergent);
}

TEST_CASE("MNA explicitly rejects unimplemented dynamic controls instead of silently degrading") {
    const auto parsed = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["out","0"],
      "ports":[{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"mode","type":"switch","pins":["out","0"],
        "switch":{"states":["open","closed"],"initial":0}}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    REQUIRE(parsed.circuit.has_value());
    const auto compiled = solo_rig::CompileMnaCircuit(*parsed.circuit);
    REQUIRE(!compiled.circuit.has_value());
    REQUIRE(compiled.error == solo_rig::MnaCompileError::UnsupportedComponent);
}

TEST_CASE("MNA rejects non-finite prepared diode model values before Prepare") {
    solo_rig::CircuitNetlist circuit{
        .version = 2,
        .schema = "circuit",
        .reference_ground = "0",
        .nodes = {"out", "0"},
        .ports = {{.id = "output", .direction = solo_rig::CircuitPortDirection::Output, .node = "out"}},
        .components = {{.id = "d",
                        .type = solo_rig::CircuitComponentType::Diode,
                        .pins = {"out", "0"},
                        .value = std::nullopt,
                        .tolerance = std::nullopt,
                        .initial = std::nullopt,
                        .model = "inline-shockley",
                        .diode_model = solo_rig::CircuitDiodeModel{.kind = "shockley",
                                                                     .saturation_current = std::numeric_limits<double>::infinity()}}},
        .solver = {}};
    const auto compiled = solo_rig::CompileMnaCircuit(circuit);
    REQUIRE(!compiled.circuit.has_value());
    REQUIRE(compiled.error == solo_rig::MnaCompileError::InvalidModel);
}

TEST_CASE("M18-C1 fixtures bind diode and Zener models through circuit v2") {
    const auto diode = solo_rig::ParseCircuitV2Json(ReadFixture("m18c1_diode_forward.json"));
    REQUIRE(diode.circuit.has_value());
    REQUIRE(diode.circuit->components.front().diode_model.has_value());
    REQUIRE(diode.circuit->components.front().diode_model->kind == "shockley");
    const auto zener = solo_rig::ParseCircuitV2Json(ReadFixture("m18c1_zener_breakdown.json"));
    REQUIRE(zener.circuit.has_value());
    REQUIRE(zener.circuit->components.front().diode_model.has_value());
    REQUIRE(zener.circuit->components.front().diode_model->kind == "zener");
    REQUIRE(zener.circuit->components.front().diode_model->breakdown_voltage == 5.1);
    const auto invalid = solo_rig::ParseCircuitV2Json(ReadFixture("m18c1_invalid_zener.json"));
    REQUIRE(!invalid.circuit.has_value());
    REQUIRE(invalid.errors.front() == solo_rig::CircuitLoadError::InvalidValue);
}

TEST_CASE("M18-C1 approved model references bind during MNA Prepare and unauthorized references fail") {
    const auto diode_database = solo_rig::BuildModelDatabase(ReadFixture("m18c1_cleanroom_diode_model.json"));
    REQUIRE(diode_database.database.has_value());
    const auto diode_graph = solo_rig::ParseCircuitV2Json(ReadFixture("m18c1_diode_reference.json"));
    REQUIRE(diode_graph.circuit.has_value());
    REQUIRE(!diode_graph.circuit->components.front().diode_model.has_value());
    const auto without_database = solo_rig::CompileMnaCircuit(*diode_graph.circuit);
    REQUIRE(without_database.error == solo_rig::MnaCompileError::MissingModelAuthorization);
    auto diode = solo_rig::CompileMnaCircuit(*diode_graph.circuit, *diode_database.database);
    REQUIRE(diode.circuit.has_value());
    REQUIRE(diode.circuit->Prepare(48000.0));
    REQUIRE(diode.circuit->Solve(1.0).Succeeded());

    const auto zener_database = solo_rig::BuildModelDatabase(ReadFixture("m18c1_cleanroom_zener_model.json"));
    REQUIRE(zener_database.database.has_value());
    const auto zener_graph = solo_rig::ParseCircuitV2Json(ReadFixture("m18c1_zener_reference.json"));
    REQUIRE(zener_graph.circuit.has_value());
    auto zener = solo_rig::CompileMnaCircuit(*zener_graph.circuit, *zener_database.database);
    REQUIRE(zener.circuit.has_value());
    REQUIRE(zener.circuit->Prepare(48000.0));
    const auto reverse = zener.circuit->Solve(-10.0);
    REQUIRE(reverse.Succeeded());
    REQUIRE(reverse.output_voltage < -5.0);

    solo_rig::ModelDatabase empty;
    const auto unauthorized = solo_rig::CompileMnaCircuit(*zener_graph.circuit, empty);
    REQUIRE(unauthorized.error == solo_rig::MnaCompileError::MissingModelAuthorization);
}

TEST_CASE("M18-C1 prepared nonlinear Solve is a noexcept preallocated boundary") {
    static_assert(noexcept(std::declval<solo_rig::MnaPreparedCircuit&>().Solve(0.0)));
    constexpr auto contract = solo_rig::MnaPreparedCircuit::ProcessContract();
    static_assert(contract.process_noexcept);
    static_assert(!contract.process_allocates);
    static_assert(!contract.process_reads_files);
    static_assert(!contract.process_parses_json);
    static_assert(!contract.process_locks);
    static_assert(contract.prepare_owns_compilation);
    REQUIRE(solo_rig::IsRealtimeSafe(contract));
    auto compiled = solo_rig::CompileMnaCircuit(
        *solo_rig::ParseCircuitV2Json(ReadFixture("m18c1_zener_breakdown.json")).circuit);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(192000.0));
    for (int sample = 0; sample < 1024; ++sample) {
        const auto solved = compiled.circuit->Solve(sample % 2 == 0 ? -10.0 : 1.0);
        REQUIRE(solved.Succeeded());
    }
}

TEST_CASE("M18-C2 BJT inline compatibility and audit references reach prepared Solve") {
    const auto inline_graph = solo_rig::ParseCircuitV2Json(ReadFixture("m18c2_bjt_inline_npn.json"));
    REQUIRE(inline_graph.circuit.has_value());
    auto inline_compiled = solo_rig::CompileMnaCircuit(*inline_graph.circuit);
    REQUIRE(inline_compiled.circuit.has_value());
    REQUIRE(inline_compiled.circuit->Prepare(192000.0));
    REQUIRE(inline_compiled.circuit->Solve(0.7).Succeeded());
    const auto reference_graph = solo_rig::ParseCircuitV2Json(ReadFixture("m18c2_bjt_reference_npn.json"));
    REQUIRE(reference_graph.circuit.has_value());
    const auto no_database = solo_rig::CompileMnaCircuit(*reference_graph.circuit);
    REQUIRE(no_database.error == solo_rig::MnaCompileError::MissingModelAuthorization);
    const auto database = solo_rig::BuildModelDatabase(ReadFixture("m18c2_cleanroom_npn_model.json"));
    REQUIRE(database.database.has_value());
    auto referenced = solo_rig::CompileMnaCircuit(*reference_graph.circuit, *database.database);
    REQUIRE(referenced.circuit.has_value());
    REQUIRE(referenced.circuit->Prepare(192000.0));
    REQUIRE(referenced.circuit->Solve(0.7).Succeeded());

    const auto unknown_graph = solo_rig::ParseCircuitV2Json(ReadFixture("m18c2_unknown_bjt_reference.json"));
    REQUIRE(unknown_graph.circuit.has_value());
    REQUIRE(solo_rig::CompileMnaCircuit(*unknown_graph.circuit, *database.database).error ==
            solo_rig::MnaCompileError::MissingModelAuthorization);

    const auto pnp_database = solo_rig::BuildModelDatabase(ReadFixture("m18c2_cleanroom_pnp_model.json"));
    const auto pnp_graph = solo_rig::ParseCircuitV2Json(ReadFixture("m18c2_bjt_reference_pnp.json"));
    REQUIRE(pnp_database.database.has_value());
    REQUIRE(pnp_graph.circuit.has_value());
    auto pnp = solo_rig::CompileMnaCircuit(*pnp_graph.circuit, *pnp_database.database);
    REQUIRE(pnp.circuit.has_value());
    REQUIRE(pnp.circuit->Prepare(192000.0));
    REQUIRE(pnp.circuit->Solve(-0.7).Succeeded());
}

TEST_CASE("M18-C2 dynamic audited BJT matches an independent backward-Euler reference and Reset") {
    const auto graph = solo_rig::ParseCircuitV2Json(ReadFixture("m18c2_dynamic_reference_npn.json"));
    const auto database = solo_rig::BuildModelDatabase(ReadFixture("m18c2_cleanroom_npn_model.json"));
    REQUIRE(graph.circuit.has_value());
    REQUIRE(database.database.has_value());
    auto compiled = solo_rig::CompileMnaCircuit(*graph.circuit, *database.database);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(48000.0));

    const auto run_sequence = [&](solo_rig::MnaPreparedCircuit& circuit) {
        double history = 5.0;
        for (const double base : {0.0, 0.45, 0.60, 0.70, 0.55, 0.20}) {
            double reference = history;
            for (std::uint32_t iteration = 0; iteration < 128U; ++iteration) {
                const auto forward = 1.0e-15 * (std::exp(base / 0.02585) - 1.0);
                const auto reverse_exp = std::exp((base - reference) / 0.02585);
                const auto reverse = 1.0e-15 * (reverse_exp - 1.0);
                const auto collector_current = (100.0 / 101.0) * forward - reverse;
                const auto collector_jacobian = 1.0e-15 / 0.02585 * reverse_exp;
                const auto residual = (reference - 5.0) / 1000.0 +
                    1.0e-6 * 48000.0 * (reference - history) + collector_current;
                const auto next = reference - residual / (1.0 / 1000.0 + 1.0e-6 * 48000.0 + collector_jacobian);
                if (std::fabs(next - reference) < 1.0e-13) {
                    reference = next;
                    break;
                }
                reference = next;
            }
            const auto solved = circuit.Solve(base);
            REQUIRE(solved.Succeeded());
            REQUIRE(std::fabs(solved.output_voltage - reference) < 1.0e-8);
            history = reference;
        }
        return history;
    };
    const auto first = run_sequence(*compiled.circuit);
    compiled.circuit->Reset();
    REQUIRE(std::fabs(run_sequence(*compiled.circuit) - first) < 1.0e-10);
}

TEST_CASE("M18-C2 audited BJT remains deterministic at 192 kHz with four-times oversampling") {
    auto graph = solo_rig::ParseCircuitV2Json(ReadFixture("m18c2_bjt_reference_npn.json"));
    const auto database = solo_rig::BuildModelDatabase(ReadFixture("m18c2_cleanroom_npn_model.json"));
    REQUIRE(graph.circuit.has_value());
    REQUIRE(database.database.has_value());
    graph.circuit->solver.oversampling = 4U;
    auto compiled = solo_rig::CompileMnaCircuit(*graph.circuit, *database.database);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(192000.0));
    constexpr auto contract = solo_rig::MnaPreparedCircuit::ProcessContract();
    static_assert(!contract.process_allocates && !contract.process_locks &&
                  !contract.process_reads_files && !contract.process_parses_json);
    for (std::uint32_t sample = 0; sample < 1024U; ++sample) {
        REQUIRE(compiled.circuit->Solve(sample % 2U == 0U ? 0.4 : 0.7).Succeeded());
    }
}

TEST_CASE("M18-C1 MNA solves forward conduction reverse breakdown and zero bias") {
    auto diode_compiled = solo_rig::CompileMnaCircuit(
        *solo_rig::ParseCircuitV2Json(ReadFixture("m18c1_diode_forward.json")).circuit);
    REQUIRE(diode_compiled.circuit.has_value());
    REQUIRE(diode_compiled.circuit->Prepare(48000.0));
    const auto zero = diode_compiled.circuit->Solve(0.0);
    const auto forward = diode_compiled.circuit->Solve(1.0);
    REQUIRE(zero.Succeeded());
    REQUIRE(std::fabs(zero.output_voltage) < 1.0e-12);
    REQUIRE(forward.Succeeded());
    REQUIRE(forward.output_voltage > 0.4);
    REQUIRE(forward.output_voltage < 0.8);

    auto zener_compiled = solo_rig::CompileMnaCircuit(
        *solo_rig::ParseCircuitV2Json(ReadFixture("m18c1_zener_breakdown.json")).circuit);
    REQUIRE(zener_compiled.circuit.has_value());
    REQUIRE(zener_compiled.circuit->Prepare(48000.0));
    const auto reverse = zener_compiled.circuit->Solve(-10.0);
    REQUIRE(reverse.Succeeded());
    REQUIRE(reverse.output_voltage < -5.0);
    REQUIRE(reverse.output_voltage > -6.5);
}

TEST_CASE("M18-C1 dynamic diode path matches an independent backward-Euler reference and Reset") {
    auto compiled = solo_rig::CompileMnaCircuit(
        *solo_rig::ParseCircuitV2Json(ReadFixture("m18c1_diode_dynamic.json")).circuit);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(1000.0));
    double history = 0.1;
    for (const double input : {1.0, 1.0, -0.25, 0.5}) {
        auto reference = history;
        for (int iteration = 0; iteration < 64; ++iteration) {
            const auto exponential = std::exp(reference / 0.02585);
            const auto current = 1.0e-12 * (exponential - 1.0);
            const auto jacobian = 1.0e-12 / 0.02585 * exponential;
            const auto residual = (reference - input) / 1000.0 +
                1.0e-6 * 1000.0 * (reference - history) + current;
            const auto next = reference - residual / (1.0 / 1000.0 + 1.0e-6 * 1000.0 + jacobian);
            if (std::fabs(next - reference) < 1.0e-13) {
                reference = next;
                break;
            }
            reference = next;
        }
        const auto solved = compiled.circuit->Solve(input);
        REQUIRE(solved.Succeeded());
        REQUIRE(std::fabs(solved.output_voltage - reference) < 1.0e-9);
        history = reference;
    }
    compiled.circuit->Reset();
    const auto reset = compiled.circuit->Solve(1.0);
    REQUIRE(reset.Succeeded());
    REQUIRE(reset.output_voltage > 0.1);
    REQUIRE(reset.output_voltage < 0.6);
}

TEST_CASE("circuit v2 parses inductor parameters and rejects invalid values") {
    const auto valid = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
      "components":[
        {"id":"l","type":"inductor","pins":["in","out"],"value":1,"initial":0.25},
        {"id":"load","type":"resistor","pins":["out","0"],"value":1000}
      ],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    REQUIRE(valid.circuit.has_value());
    REQUIRE(valid.circuit->components.front().type == solo_rig::CircuitComponentType::Inductor);
    REQUIRE(valid.circuit->components.front().initial == 0.25);
    const auto invalid = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["out","0"],
      "ports":[{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"l","type":"inductor","pins":["out","0"],"value":0}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    REQUIRE(!invalid.circuit.has_value());
    REQUIRE(invalid.errors.front() == solo_rig::CircuitLoadError::InvalidValue);
    const auto invalid_model = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["out","0"],
      "ports":[{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"l","type":"inductor","pins":["out","0"],"value":1,"model":"ideal"}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    REQUIRE(!invalid_model.circuit.has_value());
    REQUIRE(invalid_model.errors.front() == solo_rig::CircuitLoadError::InvalidValue);
}

TEST_CASE("MNA inductor uses backward Euler history and Reset restores its initial current") {
    const auto parsed = solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
      "components":[
        {"id":"l","type":"inductor","pins":["in","out"],"value":1,"initial":0},
        {"id":"load","type":"resistor","pins":["out","0"],"value":1000}
      ],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
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
    compiled.circuit->Reset();
    const auto reset = compiled.circuit->Solve(1.0);
    REQUIRE(reset.Succeeded());
    REQUIRE(std::fabs(reset.output_voltage - first.output_voltage) < 1.0e-9);
    REQUIRE(compiled.circuit->Prepare(1000.0));
    const auto prepared_again = compiled.circuit->Solve(1.0);
    REQUIRE(prepared_again.Succeeded());
    REQUIRE(std::fabs(prepared_again.output_voltage - first.output_voltage) < 1.0e-9);
}

TEST_CASE("MNA rejects a non-finite inductor initial state before Prepare") {
    solo_rig::CircuitNetlist circuit{
        .version = 2,
        .schema = "circuit",
        .reference_ground = "0",
        .nodes = {"out", "0"},
        .ports = {{.id = "output", .direction = solo_rig::CircuitPortDirection::Output, .node = "out"}},
        .components = {{.id = "l",
                        .type = solo_rig::CircuitComponentType::Inductor,
                        .pins = {"out", "0"},
                        .value = 1.0,
                        .initial = std::numeric_limits<double>::infinity()}},
        .solver = {}};
    const auto compiled = solo_rig::CompileMnaCircuit(circuit);
    REQUIRE(!compiled.circuit.has_value());
    REQUIRE(compiled.error == solo_rig::MnaCompileError::InvalidValue);
}
