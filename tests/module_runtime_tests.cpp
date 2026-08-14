#include "content/module_package.hpp"
#include "content/module_runtime.hpp"
#include "content/model_database.hpp"
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

std::string ReadWhiteboxFixture(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" / "whitebox" / name;
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
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

TEST_CASE("External analog v2 module uses the prepared inductor MNA runtime") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,"moduleId":"org.mumu-dada.solorig.external-inductor-v2",
        "displayName":"External Inductor v2","category":"analog-circuit","logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]
    })",
                                                R"({
        "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
        "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
        "components":[
            {"id":"l","type":"inductor","pins":["in","out"],"value":1},
            {"id":"load","type":"resistor","pins":["out","0"],"value":1000}
        ],
        "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
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
}

TEST_CASE("External analog v2 module uses the prepared potentiometer MNA runtime") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,"moduleId":"org.mumu-dada.solorig.external-pot-v2",
        "displayName":"External Potentiometer v2","category":"analog-circuit","logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]
    })",
                                                R"({
        "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","wiper","0"],
        "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"wiper"}],
        "components":[{"id":"p","type":"potentiometer","pins":["in","0","wiper"],"value":10000,
          "potentiometer":{"position":0.5,"minimum":0,"maximum":1,"wiperNode":"wiper"}}],
        "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
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
    REQUIRE(std::fabs(output.ChannelData(0)[0] - 0.5F) < 1.0e-5F);
    REQUIRE(!processor->SetParameter("position", 0.75F));
}

TEST_CASE("External analog v2 module uses the prepared VCCS MNA runtime") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,"moduleId":"org.mumu-dada.solorig.external-vccs-v2",
        "displayName":"External VCCS v2","category":"analog-circuit","logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]
    })",
                                                R"({
        "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
        "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
        "components":[
          {"id":"g","type":"vccs","pins":["out","0","in","0"],
           "controlledSource":{"kind":"vccs","modelVersion":1,"gain":0.001}},
          {"id":"load","type":"resistor","pins":["out","0"],"value":1000}
        ],
        "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}
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
    REQUIRE(std::fabs(output.ChannelData(0)[0] - 1.0F) < 1.0e-5F);
    REQUIRE(!processor->SetParameter("gain", 1.0F));
}

TEST_CASE("External analog v2 module uses the prepared BJT MNA runtime") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,"moduleId":"org.mumu-dada.solorig.external-bjt-v2",
        "displayName":"External BJT v2","category":"analog-circuit","logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]
    })",
                                                R"({
        "version":2,"schema":"circuit","referenceGround":"0","nodes":["collector","base","vcc","0"],
        "ports":[{"id":"input","direction":"input","node":"base"},{"id":"output","direction":"output","node":"collector"}],
        "supplies":[{"id":"vcc","positive":"vcc","negative":"0","voltage":5}],
        "components":[
          {"id":"load","type":"resistor","pins":["vcc","collector"],"value":1000},
          {"id":"q1","type":"bjt","pins":["collector","base","0"],
           "bjtModel":{"kind":"ebers-moll","modelVersion":1,"polarity":"npn","saturationCurrent":1e-15,
             "forwardBeta":100,"reverseBeta":1,"thermalVoltage":0.02585,"maxCurrent":0.005,"maxStep":10}}
        ],
        "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1,
          "convergence":{"maxIterations":64,"absoluteTolerance":1e-9,"relativeTolerance":1e-6}}
    })");
    const auto package = solo_rig::LoadModulePackage(directory);
    RemoveRuntimePackage(directory);
    REQUIRE(package.package.has_value());
    auto runtime = solo_rig::CreateModuleRuntime(*package.package);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    input.ChannelData(0)[0] = 0.6F;
    REQUIRE(processor->Prepare(TestFormat()));
    processor->Process(input, output, 1);
    REQUIRE(std::isfinite(output.ChannelData(0)[0]));
    REQUIRE(output.ChannelData(0)[0] >= 0.0F);
    REQUIRE(output.ChannelData(0)[0] <= 5.0F);
    REQUIRE(!processor->SetParameter("beta", 200.0F));
}

TEST_CASE("M18-C2 external analog v2 runtime binds an audited BJT reference before Process") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,"moduleId":"org.mumu-dada.solorig.external-audited-bjt-v2",
        "displayName":"External audited BJT v2","category":"analog-circuit","logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]
    })",
                                                R"({
        "version":2,"schema":"circuit","referenceGround":"0","nodes":["collector","base","vcc","0"],
        "ports":[{"id":"input","direction":"input","node":"base"},{"id":"output","direction":"output","node":"collector"}],
        "supplies":[{"id":"vcc","positive":"vcc","negative":"0","voltage":5}],
        "components":[
          {"id":"load","type":"resistor","pins":["vcc","collector"],"value":1000},
          {"id":"q1","type":"bjt","pins":["collector","base","0"],"model":"solorig.cleanroom.bjt.npn.v1"}
        ],
        "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4,
          "convergence":{"maxIterations":64,"absoluteTolerance":1e-9,"relativeTolerance":1e-6}}
    })");
    const auto package = solo_rig::LoadModulePackage(directory);
    RemoveRuntimePackage(directory);
    const auto database = solo_rig::BuildModelDatabase(ReadWhiteboxFixture("m18c2_cleanroom_npn_model.json"));
    REQUIRE(package.package.has_value());
    REQUIRE(database.database.has_value());
    REQUIRE(std::holds_alternative<solo_rig::ModuleRuntimeError>(
        solo_rig::CreateModuleRuntime(*package.package)));

    auto runtime = solo_rig::CreateModuleRuntime(*package.package, *database.database);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        input.ChannelData(0)[sample] = sample % 2U == 0U ? 0.4F : 0.7F;
    }
    REQUIRE(processor->Prepare({.sample_rate_hz = 192000.0, .max_block_size = 8, .channel_count = 1}));
    processor->Process(input, output, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        REQUIRE(std::isfinite(output.ChannelData(0)[sample]));
    }
}

TEST_CASE("M18-C3 external analog v2 runtime binds an audited JFET reference before Process") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,"moduleId":"org.mumu-dada.solorig.external-audited-jfet-v2",
        "displayName":"External audited JFET v2","category":"analog-circuit","logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]
    })",
                                                R"({
        "version":2,"schema":"circuit","referenceGround":"0","nodes":["drain","gate","vdd","0"],
        "ports":[{"id":"input","direction":"input","node":"gate"},{"id":"output","direction":"output","node":"drain"}],
        "supplies":[{"id":"vdd","positive":"vdd","negative":"0","voltage":5}],
        "components":[
          {"id":"load","type":"resistor","pins":["vdd","drain"],"value":1000},
          {"id":"j1","type":"jfet","pins":["drain","gate","0"],"model":"solorig.cleanroom.jfet.n-channel.v1"}
        ],
        "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4,
          "convergence":{"maxIterations":64,"absoluteTolerance":1e-9,"relativeTolerance":1e-6}}
    })");
    const auto package = solo_rig::LoadModulePackage(directory);
    RemoveRuntimePackage(directory);
    const auto database = solo_rig::BuildModelDatabase(ReadWhiteboxFixture("m18c3_cleanroom_n_jfet_model.json"));
    REQUIRE(package.package.has_value());
    REQUIRE(database.database.has_value());
    REQUIRE(std::holds_alternative<solo_rig::ModuleRuntimeError>(
        solo_rig::CreateModuleRuntime(*package.package)));

    auto runtime = solo_rig::CreateModuleRuntime(*package.package, *database.database);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        input.ChannelData(0)[sample] = sample % 2U == 0U ? -0.5F : 0.25F;
    }
    REQUIRE(processor->Prepare({.sample_rate_hz = 192000.0, .max_block_size = 8, .channel_count = 1}));
    processor->Process(input, output, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        REQUIRE(std::isfinite(output.ChannelData(0)[sample]));
        REQUIRE(output.ChannelData(0)[sample] >= 0.0F);
        REQUIRE(output.ChannelData(0)[sample] <= 5.0F);
    }
}

TEST_CASE("M18-C4 external analog v2 runtime binds an audited MOSFET reference before Process") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,"moduleId":"org.mumu-dada.solorig.external-audited-mosfet-v2",
        "displayName":"External audited MOSFET v2","category":"analog-circuit","logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]
    })",
                                                R"({
        "version":2,"schema":"circuit","referenceGround":"0","nodes":["drain","gate","vdd","0"],
        "ports":[{"id":"input","direction":"input","node":"gate"},{"id":"output","direction":"output","node":"drain"}],
        "supplies":[{"id":"vdd","positive":"vdd","negative":"0","voltage":5}],
        "components":[
          {"id":"load","type":"resistor","pins":["vdd","drain"],"value":1000},
          {"id":"m1","type":"mosfet","pins":["drain","gate","0"],"model":"solorig.cleanroom.mosfet.n-channel.v1"}
        ],
        "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4,
          "convergence":{"maxIterations":64,"absoluteTolerance":1e-9,"relativeTolerance":1e-6}}
    })");
    const auto package = solo_rig::LoadModulePackage(directory);
    RemoveRuntimePackage(directory);
    const auto database = solo_rig::BuildModelDatabase(ReadWhiteboxFixture("m18c4_cleanroom_n_mosfet_model.json"));
    REQUIRE(package.package.has_value());
    REQUIRE(database.database.has_value());
    REQUIRE(std::holds_alternative<solo_rig::ModuleRuntimeError>(
        solo_rig::CreateModuleRuntime(*package.package)));

    auto runtime = solo_rig::CreateModuleRuntime(*package.package, *database.database);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        input.ChannelData(0)[sample] = sample % 2U == 0U ? 1.5F : 2.25F;
    }
    REQUIRE(processor->Prepare({.sample_rate_hz = 192000.0, .max_block_size = 8, .channel_count = 1}));
    processor->Process(input, output, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        REQUIRE(std::isfinite(output.ChannelData(0)[sample]));
        REQUIRE(output.ChannelData(0)[sample] >= 0.0F);
        REQUIRE(output.ChannelData(0)[sample] <= 5.0F);
    }
}

TEST_CASE("M18-C5 external analog v2 runtime binds an audited OTA reference before Process") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,"moduleId":"org.mumu-dada.solorig.external-audited-ota-v2",
        "displayName":"External audited OTA v2","category":"analog-circuit","logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]
    })", R"({
        "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
        "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
        "components":[{"id":"g","type":"ota","pins":["out","0","in","0"],"model":"solorig.cleanroom.ota.bounded.v1"},{"id":"load","type":"resistor","pins":["out","0"],"value":1000}],
        "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4,
          "convergence":{"maxIterations":64,"absoluteTolerance":1e-9,"relativeTolerance":1e-6}}
    })");
    const auto package = solo_rig::LoadModulePackage(directory);
    RemoveRuntimePackage(directory);
    const auto database = solo_rig::BuildModelDatabase(ReadWhiteboxFixture("m18c5_cleanroom_ota_model.json"));
    REQUIRE(package.package.has_value());
    REQUIRE(database.database.has_value());
    REQUIRE(std::holds_alternative<solo_rig::ModuleRuntimeError>(solo_rig::CreateModuleRuntime(*package.package)));
    auto runtime = solo_rig::CreateModuleRuntime(*package.package, *database.database);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        input.ChannelData(0)[sample] = sample % 2U == 0U ? -3.0F : 3.0F;
    }
    REQUIRE(processor->Prepare({.sample_rate_hz = 192000.0, .max_block_size = 8, .channel_count = 1}));
    processor->Process(input, output, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        REQUIRE(std::isfinite(output.ChannelData(0)[sample]));
        REQUIRE(std::fabs(output.ChannelData(0)[sample]) <= 2.0F);
    }
}

TEST_CASE("M18-C6 external analog v2 runtime binds an audited triode reference before Process") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,"moduleId":"org.mumu-dada.solorig.external-audited-triode-v2",
        "displayName":"External audited triode v2","category":"analog-circuit","logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]
    })", R"({
        "version":2,"schema":"circuit","referenceGround":"0","nodes":["vdd","p","g","0"],
        "ports":[{"id":"input","direction":"input","node":"g"},{"id":"output","direction":"output","node":"p"}],
        "supplies":[{"id":"vdd","positive":"vdd","negative":"0","voltage":12}],
        "components":[{"id":"t","type":"triode","pins":["p","g","0"],"model":"solorig.cleanroom.triode.space-charge.v1"},{"id":"rp","type":"resistor","pins":["vdd","p"],"value":10000}],
        "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4,
          "convergence":{"maxIterations":64,"absoluteTolerance":1e-9,"relativeTolerance":1e-6}}
    })");
    const auto package = solo_rig::LoadModulePackage(directory);
    RemoveRuntimePackage(directory);
    const auto database = solo_rig::BuildModelDatabase(ReadWhiteboxFixture("m18c6_cleanroom_triode_model.json"));
    REQUIRE(package.package.has_value());
    REQUIRE(database.database.has_value());
    REQUIRE(std::holds_alternative<solo_rig::ModuleRuntimeError>(solo_rig::CreateModuleRuntime(*package.package)));
    auto runtime = solo_rig::CreateModuleRuntime(*package.package, *database.database);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        input.ChannelData(0)[sample] = sample % 2U == 0U ? -0.5F : 0.2F;
    }
    REQUIRE(processor->Prepare({.sample_rate_hz = 192000.0, .max_block_size = 8, .channel_count = 1}));
    processor->Process(input, output, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        REQUIRE(std::isfinite(output.ChannelData(0)[sample]));
        REQUIRE(output.ChannelData(0)[sample] >= 0.0F);
        REQUIRE(output.ChannelData(0)[sample] <= 12.0F);
    }
}

TEST_CASE("M18-C7a external analog v2 runtime binds an audited pentode reference before Process") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,"moduleId":"org.mumu-dada.solorig.external-audited-pentode-v2",
        "displayName":"External audited pentode v2","category":"analog-circuit","logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]
    })", R"({
        "version":2,"schema":"circuit","referenceGround":"0","nodes":["vdd","s","p","g","0"],
        "ports":[{"id":"input","direction":"input","node":"g"},{"id":"output","direction":"output","node":"p"}],
        "supplies":[{"id":"vdd","positive":"vdd","negative":"0","voltage":12},{"id":"screen","positive":"s","negative":"0","voltage":6}],
        "components":[{"id":"v1","type":"pentode","pins":["p","s","g","0"],"model":"solorig.cleanroom.pentode.space-charge.v1"},{"id":"rp","type":"resistor","pins":["vdd","p"],"value":10000}],
        "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4,
          "convergence":{"maxIterations":64,"absoluteTolerance":1e-9,"relativeTolerance":1e-6}}
    })");
    const auto package = solo_rig::LoadModulePackage(directory);
    RemoveRuntimePackage(directory);
    const auto database = solo_rig::BuildModelDatabase(ReadWhiteboxFixture("m18c7a_cleanroom_pentode_model.json"));
    REQUIRE(package.package.has_value());
    REQUIRE(database.database.has_value());
    REQUIRE(std::holds_alternative<solo_rig::ModuleRuntimeError>(solo_rig::CreateModuleRuntime(*package.package)));
    auto runtime = solo_rig::CreateModuleRuntime(*package.package, *database.database);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) input.ChannelData(0)[sample] = sample % 2U ? 0.2F : -0.5F;
    REQUIRE(processor->Prepare({.sample_rate_hz=192000.0,.max_block_size=8,.channel_count=1}));
    processor->Process(input, output, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        REQUIRE(std::isfinite(output.ChannelData(0)[sample]));
        REQUIRE(output.ChannelData(0)[sample] >= 0.0F);
        REQUIRE(output.ChannelData(0)[sample] <= 12.0F);
    }
}

TEST_CASE("M18-C7b external analog v2 runtime binds an audited beam tetrode reference before Process") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,"moduleId":"org.mumu-dada.solorig.external-audited-beam-tetrode-v2",
        "displayName":"External audited beam tetrode v2","category":"analog-circuit","logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]
    })", R"({
        "version":2,"schema":"circuit","referenceGround":"0","nodes":["vdd","s","p","g","0"],
        "ports":[{"id":"input","direction":"input","node":"g"},{"id":"output","direction":"output","node":"p"}],
        "supplies":[{"id":"vdd","positive":"vdd","negative":"0","voltage":12},{"id":"screen","positive":"s","negative":"0","voltage":6}],
        "components":[{"id":"v1","type":"beam-tetrode","pins":["p","s","g","0"],"model":"solorig.cleanroom.beam-tetrode.capture.v1"},{"id":"rp","type":"resistor","pins":["vdd","p"],"value":10000}],
        "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4,
          "convergence":{"maxIterations":64,"absoluteTolerance":1e-9,"relativeTolerance":1e-6}}
    })");
    const auto package = solo_rig::LoadModulePackage(directory);
    RemoveRuntimePackage(directory);
    const auto database = solo_rig::BuildModelDatabase(
        ReadWhiteboxFixture("m18c7b_cleanroom_beam_tetrode_model.json"));
    REQUIRE(package.package.has_value());
    REQUIRE(database.database.has_value());
    REQUIRE(std::holds_alternative<solo_rig::ModuleRuntimeError>(solo_rig::CreateModuleRuntime(*package.package)));
    auto runtime = solo_rig::CreateModuleRuntime(*package.package, *database.database);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) input.ChannelData(0)[sample] = sample % 2U ? 0.2F : -0.5F;
    REQUIRE(processor->Prepare({.sample_rate_hz=192000.0,.max_block_size=8,.channel_count=1}));
    processor->Process(input, output, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        REQUIRE(std::isfinite(output.ChannelData(0)[sample]));
        REQUIRE(output.ChannelData(0)[sample] >= 0.0F);
        REQUIRE(output.ChannelData(0)[sample] <= 12.0F);
    }
}

TEST_CASE("M18-C7c external analog v2 runtime binds an audited vari-mu reference before Process") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,"moduleId":"org.mumu-dada.solorig.external-audited-vari-mu-v2",
        "displayName":"External audited vari-mu v2","category":"analog-circuit","logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]
    })", R"({
        "version":2,"schema":"circuit","referenceGround":"0","nodes":["vdd","p","g","0"],
        "ports":[{"id":"input","direction":"input","node":"g"},{"id":"output","direction":"output","node":"p"}],
        "supplies":[{"id":"vdd","positive":"vdd","negative":"0","voltage":12}],
        "components":[{"id":"v1","type":"vari-mu","pins":["p","g","0"],"model":"solorig.cleanroom.vari-mu.remote-cutoff.v1"},{"id":"rp","type":"resistor","pins":["vdd","p"],"value":10000}],
        "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4,
          "convergence":{"maxIterations":64,"absoluteTolerance":1e-9,"relativeTolerance":1e-6}}
    })");
    const auto package = solo_rig::LoadModulePackage(directory);
    RemoveRuntimePackage(directory);
    const auto database = solo_rig::BuildModelDatabase(
        ReadWhiteboxFixture("m18c7c_cleanroom_vari_mu_model.json"));
    REQUIRE(package.package.has_value());
    REQUIRE(database.database.has_value());
    REQUIRE(std::holds_alternative<solo_rig::ModuleRuntimeError>(solo_rig::CreateModuleRuntime(*package.package)));
    auto runtime = solo_rig::CreateModuleRuntime(*package.package, *database.database);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        input.ChannelData(0)[sample] = sample % 2U ? 0.2F : -0.5F;
    }
    REQUIRE(processor->Prepare({.sample_rate_hz=192000.0,.max_block_size=8,.channel_count=1}));
    processor->Process(input, output, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        REQUIRE(std::isfinite(output.ChannelData(0)[sample]));
        REQUIRE(output.ChannelData(0)[sample] >= 0.0F);
        REQUIRE(output.ChannelData(0)[sample] <= 12.0F);
    }
}

TEST_CASE("M18-C8a external analog v2 runtime binds an audited coupled-inductor before Process") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,"moduleId":"org.mumu-dada.solorig.external-audited-coupled-inductor-v2",
        "displayName":"External audited coupled inductor v2","category":"analog-circuit","logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]
    })", R"({
        "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
        "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
        "components":[
          {"id":"t1","type":"coupled-inductor","pins":["in","0","out","0"],"model":"solorig.cleanroom.transformer-core.lossy-linear.v1"},
          {"id":"load","type":"resistor","pins":["out","0"],"value":1000.0}],
        "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4}
    })");
    const auto package = solo_rig::LoadModulePackage(directory);
    RemoveRuntimePackage(directory);
    const auto database = solo_rig::BuildModelDatabase(
        ReadWhiteboxFixture("m18c8a_cleanroom_transformer_model.json"));
    REQUIRE(package.package.has_value());
    REQUIRE(database.database.has_value());
    REQUIRE(std::holds_alternative<solo_rig::ModuleRuntimeError>(
        solo_rig::CreateModuleRuntime(*package.package)));
    auto runtime = solo_rig::CreateModuleRuntime(*package.package, *database.database);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        input.ChannelData(0)[sample] = sample % 2U ? 1.0F : -1.0F;
    }
    REQUIRE(processor->Prepare({.sample_rate_hz=192000.0,.max_block_size=8,.channel_count=1}));
    processor->Process(input, output, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        REQUIRE(std::isfinite(output.ChannelData(0)[sample]));
        REQUIRE(std::fabs(output.ChannelData(0)[sample]) <= 1.0F);
    }
}

TEST_CASE("M18-C8b external analog v2 runtime binds an audited nonlinear transformer before Process") {
    const auto directory = CreateRuntimePackage(R"({
        "schemaVersion":1,"moduleId":"org.mumu-dada.solorig.external-audited-nonlinear-transformer-v2",
        "displayName":"External audited nonlinear transformer v2","category":"analog-circuit","logicFile":"logic/main.json",
        "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]
    })", R"({
        "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
        "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
        "components":[
          {"id":"t1","type":"coupled-inductor","pins":["in","0","out","0"],"model":"solorig.cleanroom.transformer-core.saturating-hysteretic.v1"},
          {"id":"load","type":"resistor","pins":["out","0"],"value":1000.0}],
        "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4}
    })");
    const auto package = solo_rig::LoadModulePackage(directory);
    RemoveRuntimePackage(directory);
    const auto database = solo_rig::BuildModelDatabase(
        ReadWhiteboxFixture("m18c8b_cleanroom_transformer_model.json"));
    REQUIRE(package.package.has_value());
    REQUIRE(database.database.has_value());
    auto runtime = solo_rig::CreateModuleRuntime(*package.package, *database.database);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        input.ChannelData(0)[sample] = sample % 2U ? 3.0F : -3.0F;
    }
    REQUIRE(processor->Prepare({.sample_rate_hz=192000.0,.max_block_size=8,.channel_count=1}));
    processor->Process(input, output, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        REQUIRE(std::isfinite(output.ChannelData(0)[sample]));
        REQUIRE(std::fabs(output.ChannelData(0)[sample]) <= 3.0F);
    }
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
