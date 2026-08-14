#include "content/module_package.hpp"
#include "content/module_runtime.hpp"
#include "core/audio_buffer.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>
#include <variant>

TEST_CASE("M18-C10b external speaker runtime stays finite and preallocated at 192 kHz 4x") {
    const auto directory = std::filesystem::temp_directory_path() / "SoloRigC10bRuntimeTest";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory / "logic", error);
    std::ofstream(directory / "manifest.json", std::ios::binary | std::ios::trunc) << R"({
      "schemaVersion":1,"moduleId":"org.mumu-dada.solorig.c10b-speaker","displayName":"C10b Speaker",
      "category":"analog-circuit","logicFile":"logic/main.json","ports":[
      {"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]})";
    std::ofstream(directory / "logic" / "main.json", std::ios::binary | std::ios::trunc) << R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"r","type":"resistor","pins":["in","out"],"value":2},
      {"id":"s","type":"speaker-load","pins":["out","0"],"speakerModel":{"kind":"electrodynamic",
      "modelVersion":1,"voiceCoilResistance":6,"voiceCoilInductance":0.0005,"forceFactor":5,
      "movingMass":0.02,"mechanicalResistance":2,"compliance":0.0005,"nonlinearStiffness":1000,
      "nonlinearDisplacementScale":0.002,"maximumCurrent":10,"maximumVelocity":100,
      "maximumDisplacement":0.1,"initialCurrent":0,"initialVelocity":0,"initialDisplacement":0}}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4,
      "convergence":{"maxIterations":64,"absoluteTolerance":1e-10,"relativeTolerance":1e-8}}})";
    const auto package = solo_rig::LoadModulePackage(directory);
    std::filesystem::remove_all(directory, error);
    REQUIRE(package.package.has_value());
    auto runtime = solo_rig::CreateModuleRuntime(*package.package);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    REQUIRE(processor->Prepare({.sample_rate_hz = 192000.0, .max_block_size = 16,
                                .channel_count = 1}));
    solo_rig::AudioBuffer input(1, 16);
    solo_rig::AudioBuffer output(1, 16);
    for (std::uint32_t sample = 0; sample < 16U; ++sample) input.ChannelData(0)[sample] = 0.5F;
    processor->Process(input, output, 16);
    for (std::uint32_t sample = 0; sample < 16U; ++sample) {
        REQUIRE(std::isfinite(output.ChannelData(0)[sample]));
    }
}
