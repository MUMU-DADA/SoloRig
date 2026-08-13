#include "content/module_package.hpp"
#include "content/module_runtime.hpp"
#include "core/audio_buffer.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>
#include <variant>

namespace {

std::filesystem::path CreateRuntimePackage(std::string_view manifest, std::string_view logic) {
    const auto directory = std::filesystem::temp_directory_path() / "SoloRigModuleRuntimeTest";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory / "logic", error);
    {
        std::ofstream output(directory / "manifest.json", std::ios::binary | std::ios::trunc);
        output << manifest;
    }
    {
        std::ofstream output(directory / "logic" / "main.json", std::ios::binary | std::ios::trunc);
        output << logic;
    }
    return directory;
}

void RemoveRuntimePackage(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

solo_rig::AudioFormat TestFormat() {
    return {.sample_rate_hz = 48000.0, .max_block_size = 8, .channel_count = 1};
}

} // namespace

TEST_CASE("External digital module package instantiates and executes its declared gain algorithm") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,
        "moduleId":"org.mumu-dada.solorig.external-gain",
        "displayName":"External Gain",
        "category":"digital-algorithm",
        "logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}],
        "parameters":[{"id":"gain","minimum":0,"maximum":2,"default":0.5}]
    })",
                                                R"({
        "schemaVersion":1,
        "kind":"digital-algorithm",
        "algorithm":"linear-gain",
        "operations":[{"type":"gain","parameter":"gain"}]
    })");
    const auto package = solo_rig::LoadModulePackage(directory);
    RemoveRuntimePackage(directory);
    REQUIRE(package.package.has_value());

    auto runtime = solo_rig::CreateModuleRuntime(*package.package);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    input.ChannelData(0)[0] = 0.4F;

    REQUIRE(processor->Prepare(TestFormat()));
    processor->Process(input, output, 1);
    REQUIRE(output.ChannelData(0)[0] == 0.2F);
    REQUIRE(processor->SetParameter("gain", 1.0F));
    processor->Process(input, output, 1);
    REQUIRE(output.ChannelData(0)[0] == 0.4F);
    REQUIRE(!processor->SetParameter("gain", 3.0F));
}

TEST_CASE("External analog module package instantiates a supported MNA diode clipper topology") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,
        "moduleId":"org.mumu-dada.solorig.external-clipper",
        "displayName":"External Clipper",
        "category":"analog-circuit",
        "logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}],
        "parameters":[{"id":"drive","minimum":0,"maximum":8,"default":4},{"id":"outputGain","minimum":0,"maximum":2,"default":1}]
    })",
                                                R"({
        "schemaVersion":1,
        "kind":"analog-circuit",
        "solver":"modified-nodal-analysis",
        "components":[
            {"id":"input-resistor","type":"resistor","nodes":["input","signal"],"value":4700},
            {"id":"clipper-positive","type":"diode","nodes":["signal","ground"],"model":"silicon"},
            {"id":"clipper-negative","type":"diode","nodes":["signal","ground"],"model":"silicon"}
        ]
    })");
    const auto package = solo_rig::LoadModulePackage(directory);
    RemoveRuntimePackage(directory);
    REQUIRE(package.package.has_value());

    auto runtime = solo_rig::CreateModuleRuntime(*package.package);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    input.ChannelData(0)[0] = 1.0F;

    REQUIRE(processor->Prepare(TestFormat()));
    processor->Process(input, output, 1);
    REQUIRE(std::isfinite(output.ChannelData(0)[0]));
    REQUIRE(output.ChannelData(0)[0] > 0.35F);
    REQUIRE(output.ChannelData(0)[0] < 0.71F);
}

TEST_CASE("External analog v2 module uses the unified linear MNA runtime") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,
        "moduleId":"org.mumu-dada.solorig.external-linear-divider",
        "displayName":"External Linear Divider",
        "category":"analog-circuit",
        "logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]
    })",
                                                R"({
        "version":2,
        "schema":"circuit",
        "referenceGround":"0",
        "nodes":["in","out","0"],
        "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
        "components":[
            {"id":"series","type":"resistor","pins":["in","out"],"value":1000},
            {"id":"load","type":"resistor","pins":["out","0"],"value":1000}
        ],
        "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
    })");
    const auto package = solo_rig::LoadModulePackage(directory);
    RemoveRuntimePackage(directory);
    REQUIRE(package.package.has_value());
    REQUIRE(std::holds_alternative<solo_rig::CircuitNetlist>(package.package->logic));

    auto runtime = solo_rig::CreateModuleRuntime(*package.package);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    input.ChannelData(0)[0] = 1.0F;
    REQUIRE(processor->Prepare(TestFormat()));
    processor->Process(input, output, 1);
    REQUIRE(std::fabs(output.ChannelData(0)[0] - 0.5F) < 1.0e-6F);
}

TEST_CASE("External analog v2 module uses the prepared nonlinear diode MNA runtime") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,
        "moduleId":"org.mumu-dada.solorig.external-diode-v2",
        "displayName":"External Diode v2",
        "category":"analog-circuit",
        "logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]
    })",
                                                R"({
        "version":2,"schema":"circuit","referenceGround":"0",
        "nodes":["in","out","0"],
        "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
        "components":[
            {"id":"series","type":"resistor","pins":["in","out"],"value":1000},
            {"id":"diode","type":"diode","pins":["out","0"],
             "model":{"kind":"shockley","saturationCurrent":1e-9,"thermalVoltage":0.02585,"maxStep":1}}
        ],
        "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1,
          "convergence":{"maxIterations":32,"absoluteTolerance":1e-9,"relativeTolerance":1e-6}}
    })");
    const auto package = solo_rig::LoadModulePackage(directory);
    RemoveRuntimePackage(directory);
    REQUIRE(package.package.has_value());
    auto runtime = solo_rig::CreateModuleRuntime(*package.package);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    input.ChannelData(0)[0] = 0.5F;
    REQUIRE(processor->Prepare(TestFormat()));
    processor->Process(input, output, 1);
    REQUIRE(std::isfinite(output.ChannelData(0)[0]));
    REQUIRE(output.ChannelData(0)[0] > 0.0F);
    REQUIRE(output.ChannelData(0)[0] < 0.5F);
}

TEST_CASE("External digital delay package executes declared delay feedback and mix operations") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,
        "moduleId":"org.mumu-dada.solorig.external-delay",
        "displayName":"External Delay",
        "category":"digital-algorithm",
        "logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}],
        "parameters":[
            {"id":"time","minimum":0,"maximum":0.1,"default":0.0000416667},
            {"id":"feedback","minimum":0,"maximum":0.99,"default":0},
            {"id":"mix","minimum":0,"maximum":1,"default":1}
        ]
    })",
                                                R"({
        "schemaVersion":1,
        "kind":"digital-algorithm",
        "algorithm":"feedback-delay",
        "operations":[
            {"type":"delay","parameter":"time"},
            {"type":"feedback","parameter":"feedback"},
            {"type":"mix","parameter":"mix"}
        ]
    })");
    const auto package = solo_rig::LoadModulePackage(directory);
    RemoveRuntimePackage(directory);
    REQUIRE(package.package.has_value());

    auto runtime = solo_rig::CreateModuleRuntime(*package.package);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    input.ChannelData(0)[0] = 1.0F;

    REQUIRE(processor->Prepare(TestFormat()));
    processor->Process(input, output, 4);
    REQUIRE(output.ChannelData(0)[0] == 0.0F);
    REQUIRE(output.ChannelData(0)[1] == 0.0F);
    REQUIRE(output.ChannelData(0)[2] == 1.0F);
}

TEST_CASE("External module runtime rejects declared algorithms it cannot execute") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,
        "moduleId":"org.mumu-dada.solorig.unsupported-filter",
        "displayName":"Unsupported Filter",
        "category":"digital-algorithm",
        "logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]
    })",
                                                R"({
        "schemaVersion":1,
        "kind":"digital-algorithm",
        "algorithm":"unimplemented-filter",
        "operations":[{"type":"filter"}]
    })");
    const auto package = solo_rig::LoadModulePackage(directory);
    RemoveRuntimePackage(directory);
    REQUIRE(package.package.has_value());

    const auto runtime = solo_rig::CreateModuleRuntime(*package.package);

    REQUIRE(std::holds_alternative<solo_rig::ModuleRuntimeError>(runtime));
    REQUIRE(std::get<solo_rig::ModuleRuntimeError>(runtime) == solo_rig::ModuleRuntimeError::UnsupportedDigitalOperation);
}

TEST_CASE("External module runtime rejects analog topologies beyond its verified MNA adapter") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,
        "moduleId":"org.mumu-dada.solorig.unsupported-inductor",
        "displayName":"Unsupported Inductor",
        "category":"analog-circuit",
        "logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]
    })",
                                                R"({
        "schemaVersion":1,
        "kind":"analog-circuit",
        "solver":"modified-nodal-analysis",
        "components":[
            {"id":"input-resistor","type":"resistor","nodes":["input","signal"],"value":4700},
            {"id":"inductor","type":"inductor","nodes":["signal","ground"],"value":0.01},
            {"id":"diode","type":"diode","nodes":["signal","ground"],"model":"silicon"}
        ]
    })");
    const auto package = solo_rig::LoadModulePackage(directory);
    RemoveRuntimePackage(directory);
    REQUIRE(package.package.has_value());

    const auto runtime = solo_rig::CreateModuleRuntime(*package.package);

    REQUIRE(std::holds_alternative<solo_rig::ModuleRuntimeError>(runtime));
    REQUIRE(std::get<solo_rig::ModuleRuntimeError>(runtime) == solo_rig::ModuleRuntimeError::UnsupportedAnalogTopology);
}
