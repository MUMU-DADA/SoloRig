#include "content/module_package.hpp"
#include "content/module_runtime.hpp"
#include "core/audio_buffer.hpp"
#include "dsp/mna_solver.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>
#include <variant>

namespace {

std::filesystem::path CreatePackage() {
    const auto directory = std::filesystem::temp_directory_path() / "SoloRigC10aRuntimeTest";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory / "logic", error);
    std::ofstream(directory / "manifest.json", std::ios::binary | std::ios::trunc) << R"({
      "schemaVersion":1,"moduleId":"org.mumu-dada.solorig.c10a-dynamic-supply",
      "displayName":"C10a Dynamic Supply","category":"analog-circuit","logicFile":"logic/main.json",
      "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]})";
    std::ofstream(directory / "logic" / "main.json", std::ios::binary | std::ios::trunc) << R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","rail","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"rail"}],
      "supplies":[{"id":"s","positive":"rail","negative":"0","voltage":12,"kind":"rectified-reservoir",
        "modelVersion":1,"sourceResistance":10,"sagResistance":20,"recoveryTime":0.1,
        "rippleAmplitude":0.2,"rippleFrequency":100,"maximumSag":4,"maximumLoadCurrent":1,"initialSag":0}],
      "components":[{"id":"load","type":"resistor","pins":["rail","0"],"value":1000},
        {"id":"input-load","type":"resistor","pins":["in","0"],"value":1000}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4,
        "convergence":{"maxIterations":32,"absoluteTolerance":1e-10,"relativeTolerance":1e-8}}})";
    return directory;
}

} // namespace

TEST_CASE("M18-C10a external runtime processes dynamic supply at 192 kHz 4x without allocation") {
    const auto directory = CreatePackage();
    const auto package = solo_rig::LoadModulePackage(directory);
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    REQUIRE(package.package.has_value());
    auto runtime = solo_rig::CreateModuleRuntime(*package.package);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    REQUIRE(processor->Prepare({.sample_rate_hz = 192000.0, .max_block_size = 16,
                                .channel_count = 1}));
    solo_rig::AudioBuffer input(1, 16);
    solo_rig::AudioBuffer output(1, 16);
    processor->Process(input, output, 16);
    for (std::uint32_t sample = 0; sample < 16U; ++sample) {
        REQUIRE(std::isfinite(output.ChannelData(0)[sample]));
        REQUIRE(output.ChannelData(0)[sample] > 0.0F);
        REQUIRE(output.ChannelData(0)[sample] < 12.0F);
    }
    constexpr auto contract = solo_rig::MnaPreparedCircuit::ProcessContract();
    static_assert(!contract.process_allocates && !contract.process_locks &&
                  !contract.process_reads_files && !contract.process_parses_json);
}
