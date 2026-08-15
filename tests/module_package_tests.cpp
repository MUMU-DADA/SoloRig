#include "content/module_package.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <variant>

namespace {

std::filesystem::path CreateTestPackage(std::string_view manifest) {
    const auto directory = std::filesystem::temp_directory_path() / "SoloRigModulePackageTest";
    std::filesystem::create_directories(directory);
    std::ofstream output(directory / "manifest.json", std::ios::binary | std::ios::trunc);
    output << manifest;
    return directory;
}

void WriteTestPackageFile(const std::filesystem::path& directory, std::string_view relative_path,
                          std::string_view contents) {
    const auto path = directory / std::filesystem::path(relative_path);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << contents;
}

void RemoveTestPackage(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

} // namespace

TEST_CASE("LoadModulePackage loads metadata and optional custom pedal appearance") {
    const auto directory = CreateTestPackage(R"({
        "schemaVersion": 1,
        "moduleId": "org.mumu-dada.solorig.drive",
        "displayName": "Reference Drive",
        "category": "analog-circuit",
        "logicFile": "logic/circuit.json",
        "skinFile": "ui/pedal.json",
        "declaredLatencySamples": 0,
        "ports": [
            {"id":"input","direction":"input","maxChannels":2},
            {"id":"output","direction":"output","maxChannels":2}
        ],
        "parameters": [{"id":"drive","minimum":0,"maximum":1,"default":0.5}]
    })");
    WriteTestPackageFile(directory, "logic/circuit.json", R"({
        "schemaVersion": 1,
        "kind": "analog-circuit",
        "solver": "modified-nodal-analysis",
        "components": [
            {"id":"input-resistor","type":"resistor","nodes":["input","drive"],"value":470000},
            {"id":"clipper","type":"diode","nodes":["drive","ground"],"model":"silicon"}
        ]
    })");

    const auto result = solo_rig::LoadModulePackage(directory);
    RemoveTestPackage(directory);

    REQUIRE(result.errors.empty());
    REQUIRE(result.package.has_value());
    REQUIRE(result.package->descriptor.module_id == "org.mumu-dada.solorig.drive");
    REQUIRE(result.package->custom_skin_path.has_value());
}

TEST_CASE("LoadExternalModuleAsset validates and packages a direct Circuit v2 file") {
    const auto path = std::filesystem::temp_directory_path() / "solorig-direct-circuit.json";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << R"({
            "version": 2,
            "schema": "circuit",
            "referenceGround": "0",
            "nodes": ["in", "out", "0"],
            "ports": [
                {"id":"input","direction":"input","node":"in"},
                {"id":"output","direction":"output","node":"out"}
            ],
            "supplies": [],
            "components": [
                {"id":"series","type":"resistor","pins":["in","out"],"value":1000},
                {"id":"load","type":"resistor","pins":["out","0"],"value":1000}
            ],
            "solver": {
                "method":"modified-nodal-analysis",
                "integration":"backward-euler",
                "oversampling":1,
                "convergence":{"maxIterations":16,"absoluteTolerance":1e-9,"relativeTolerance":1e-6}
            }
        })";
    }
    const auto result = solo_rig::LoadExternalModuleAsset(
        path,
        solo_rig::ExternalModuleSourceKind::AnalogCircuitFile,
        "org.mumu-dada.solorig.imported.direct-circuit",
        "Direct Circuit");
    REQUIRE(result.errors.empty());
    REQUIRE(result.package.has_value());
    REQUIRE(result.package->descriptor.category == solo_rig::EffectCategory::AnalogCircuit);
    REQUIRE(result.package->descriptor.ports.size() == 2U);
    REQUIRE(std::holds_alternative<solo_rig::CircuitNetlist>(result.package->logic));
    std::error_code cleanup_error;
    std::filesystem::remove(path, cleanup_error);
}

TEST_CASE("LoadModulePackage parses an analog circuit topology for component simulation") {
    const auto directory = CreateTestPackage(R"({
        "schemaVersion": 1,
        "moduleId": "org.mumu-dada.solorig.circuit-drive",
        "displayName": "Circuit Drive",
        "category": "analog-circuit",
        "logicFile": "logic/circuit.json",
        "ports": [
            {"id":"input","direction":"input","maxChannels":1},
            {"id":"output","direction":"output","maxChannels":1}
        ],
        "parameters": [{"id":"drive","minimum":0,"maximum":1,"default":0.5}]
    })");
    WriteTestPackageFile(directory, "logic/circuit.json", R"({
        "schemaVersion": 1,
        "kind": "analog-circuit",
        "solver": "modified-nodal-analysis",
        "components": [
            {"id":"input-resistor","type":"resistor","nodes":["input","gain"],"value":10000},
            {"id":"filter-capacitor","type":"capacitor","nodes":["gain","ground"],"value":0.000000047},
            {"id":"clipping-diode","type":"diode","nodes":["gain","output"],"model":"silicon"}
        ]
    })");

    const auto result = solo_rig::LoadModulePackage(directory);
    RemoveTestPackage(directory);

    REQUIRE(result.errors.empty());
    REQUIRE(result.package.has_value());
    const auto* circuit = std::get_if<solo_rig::AnalogCircuitLogic>(&result.package->logic);
    REQUIRE(circuit != nullptr);
    REQUIRE(circuit->solver == solo_rig::AnalogCircuitSolver::ModifiedNodalAnalysis);
    REQUIRE(circuit->components.size() == 3U);
    REQUIRE(circuit->components.at(0).type == solo_rig::AnalogComponentType::Resistor);
    REQUIRE(circuit->components.at(0).value.has_value());
}

TEST_CASE("LoadModulePackage parses declared digital operations and parameter bindings") {
    const auto directory = CreateTestPackage(R"({
        "schemaVersion": 1,
        "moduleId": "org.mumu-dada.solorig.digital-delay",
        "displayName": "Digital Delay",
        "category": "digital-algorithm",
        "logicFile": "logic/algorithm.json",
        "ports": [
            {"id":"input","direction":"input","maxChannels":2},
            {"id":"output","direction":"output","maxChannels":2}
        ],
        "parameters": [
            {"id":"time","minimum":0,"maximum":1,"default":0.3},
            {"id":"mix","minimum":0,"maximum":1,"default":0.5}
        ]
    })");
    WriteTestPackageFile(directory, "logic/algorithm.json", R"({
        "schemaVersion": 1,
        "kind": "digital-algorithm",
        "algorithm": "feedback-delay",
        "operations": [
            {"type":"delay","parameter":"time"},
            {"type":"mix","parameter":"mix"}
        ]
    })");

    const auto result = solo_rig::LoadModulePackage(directory);
    RemoveTestPackage(directory);

    REQUIRE(result.errors.empty());
    REQUIRE(result.package.has_value());
    const auto* algorithm = std::get_if<solo_rig::DigitalAlgorithmLogic>(&result.package->logic);
    REQUIRE(algorithm != nullptr);
    REQUIRE(algorithm->algorithm_id == "feedback-delay");
    REQUIRE(algorithm->operations.size() == 2U);
    REQUIRE(algorithm->operations.at(0).type == solo_rig::DigitalOperationType::Delay);
    REQUIRE(algorithm->operations.at(0).parameter_id == "time");
}

TEST_CASE("LoadModulePackage rejects a logic kind that disagrees with the module category") {
    const auto directory = CreateTestPackage(R"({
        "schemaVersion": 1,
        "moduleId": "org.mumu-dada.solorig.mismatched-logic",
        "displayName": "Mismatched Logic",
        "category": "analog-circuit",
        "logicFile": "logic/algorithm.json",
        "ports": [
            {"id":"input","direction":"input","maxChannels":1},
            {"id":"output","direction":"output","maxChannels":1}
        ]
    })");
    WriteTestPackageFile(directory, "logic/algorithm.json", R"({
        "schemaVersion": 1,
        "kind": "digital-algorithm",
        "algorithm": "gain",
        "operations": [{"type":"gain"}]
    })");

    const auto result = solo_rig::LoadModulePackage(directory);
    RemoveTestPackage(directory);

    REQUIRE(!result.package.has_value());
    REQUIRE(!result.errors.empty());
    REQUIRE(result.errors.front() == solo_rig::ModulePackageLoadError::LogicCategoryMismatch);
}

TEST_CASE("LoadModulePackage rejects analog logic without a component netlist") {
    const auto directory = CreateTestPackage(R"({
        "schemaVersion": 1,
        "moduleId": "org.mumu-dada.solorig.empty-circuit",
        "displayName": "Empty Circuit",
        "category": "analog-circuit",
        "logicFile": "logic/circuit.json",
        "ports": [
            {"id":"input","direction":"input","maxChannels":1},
            {"id":"output","direction":"output","maxChannels":1}
        ]
    })");
    WriteTestPackageFile(directory, "logic/circuit.json", R"({
        "schemaVersion": 1,
        "kind": "analog-circuit",
        "solver": "modified-nodal-analysis",
        "components": []
    })");

    const auto result = solo_rig::LoadModulePackage(directory);
    RemoveTestPackage(directory);

    REQUIRE(!result.package.has_value());
    REQUIRE(!result.errors.empty());
    REQUIRE(result.errors.front() == solo_rig::ModulePackageLoadError::InvalidLogicDefinition);
}

TEST_CASE("LoadModulePackage rejects a missing required analog logic file") {
    const auto directory = CreateTestPackage(R"({
        "schemaVersion": 1,
        "moduleId": "org.mumu-dada.solorig.missing-circuit",
        "displayName": "Missing Circuit",
        "category": "analog-circuit",
        "logicFile": "logic/circuit.json",
        "ports": [
            {"id":"input","direction":"input","maxChannels":1},
            {"id":"output","direction":"output","maxChannels":1}
        ]
    })");

    const auto result = solo_rig::LoadModulePackage(directory);
    RemoveTestPackage(directory);

    REQUIRE(!result.package.has_value());
    REQUIRE(!result.errors.empty());
    REQUIRE(result.errors.front() == solo_rig::ModulePackageLoadError::MissingLogicFile);
}

TEST_CASE("LoadModulePackage rejects digital operations bound to undeclared parameters") {
    const auto directory = CreateTestPackage(R"({
        "schemaVersion": 1,
        "moduleId": "org.mumu-dada.solorig.invalid-binding",
        "displayName": "Invalid Binding",
        "category": "digital-algorithm",
        "logicFile": "logic/algorithm.json",
        "ports": [
            {"id":"input","direction":"input","maxChannels":1},
            {"id":"output","direction":"output","maxChannels":1}
        ],
        "parameters": [{"id":"mix","minimum":0,"maximum":1,"default":0.5}]
    })");
    WriteTestPackageFile(directory, "logic/algorithm.json", R"({
        "schemaVersion": 1,
        "kind": "digital-algorithm",
        "algorithm": "gain",
        "operations": [{"type":"gain","parameter":"missing"}]
    })");

    const auto result = solo_rig::LoadModulePackage(directory);
    RemoveTestPackage(directory);

    REQUIRE(!result.package.has_value());
    REQUIRE(!result.errors.empty());
    REQUIRE(result.errors.front() == solo_rig::ModulePackageLoadError::LogicParameterUnknown);
}

TEST_CASE("LoadModulePackage rejects asset paths that escape the package directory") {
    const auto directory = CreateTestPackage(R"({
        "schemaVersion": 1,
        "moduleId": "org.mumu-dada.solorig.unsafe",
        "displayName": "Unsafe",
        "category": "digital-algorithm",
        "logicFile": "../outside.json",
        "ports": [
            {"id":"input","direction":"input","maxChannels":1},
            {"id":"output","direction":"output","maxChannels":1}
        ]
    })");

    const auto result = solo_rig::LoadModulePackage(directory);
    RemoveTestPackage(directory);

    REQUIRE(!result.package.has_value());
    REQUIRE(!result.errors.empty());
    REQUIRE(result.errors.front() == solo_rig::ModulePackageLoadError::UnsafeAssetPath);
}

TEST_CASE("IsSafeRelativeAssetPath rejects absolute and traversal paths") {
    REQUIRE(solo_rig::IsSafeRelativeAssetPath("logic/algorithm.json"));
    REQUIRE(!solo_rig::IsSafeRelativeAssetPath("../logic.json"));
    REQUIRE(!solo_rig::IsSafeRelativeAssetPath("C:/logic.json"));
}
