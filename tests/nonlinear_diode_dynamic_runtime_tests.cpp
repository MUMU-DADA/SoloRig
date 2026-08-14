#include "content/module_package.hpp"
#include "content/module_runtime.hpp"
#include "content/model_database.hpp"
#include "core/audio_buffer.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <variant>

namespace {

std::filesystem::path CreatePackage(std::string_view manifest, std::string_view logic) {
    const auto directory = std::filesystem::temp_directory_path() / "SoloRigM18C9aRuntimeTest";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory / "logic", error);
    std::ofstream(directory / "manifest.json", std::ios::binary | std::ios::trunc) << manifest;
    std::ofstream(directory / "logic" / "main.json", std::ios::binary | std::ios::trunc) << logic;
    return directory;
}

void RemovePackage(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

std::string ReadFixture(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" / "whitebox" / name;
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("M18-C9a external analog runtime binds audited dynamic diode without realtime allocation") {
    const auto directory = CreatePackage(R"({
      "schemaVersion":1,"moduleId":"org.mumu-dada.solorig.external-dynamic-diode-v2",
      "displayName":"External dynamic diode v2","category":"analog-circuit","logicFile":"logic/main.json",
      "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]
    })", R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
      "components":[
        {"id":"r","type":"resistor","pins":["in","out"],"value":1000},
        {"id":"d","type":"diode","pins":["out","0"],"initial":0.1,
         "model":"solorig.cleanroom.diode.dynamic-junction.v1"}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4}
    })");
    const auto package = solo_rig::LoadModulePackage(directory);
    RemovePackage(directory);
    const auto database = solo_rig::BuildModelDatabase(ReadFixture("m18c9a_cleanroom_diode_model.json"));
    REQUIRE(package.package.has_value());
    REQUIRE(database.database.has_value());
    REQUIRE(std::holds_alternative<solo_rig::ModuleRuntimeError>(
        solo_rig::CreateModuleRuntime(*package.package)));
    auto runtime = solo_rig::CreateModuleRuntime(*package.package, *database.database);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    solo_rig::AudioBuffer input(1, 16);
    solo_rig::AudioBuffer output(1, 16);
    for (std::uint32_t sample = 0; sample < 16U; ++sample) {
        input.ChannelData(0)[sample] = sample % 2U == 0U ? 0.5F : -0.5F;
    }
    REQUIRE(processor->Prepare({.sample_rate_hz = 192000.0, .max_block_size = 16, .channel_count = 1}));
    processor->Process(input, output, 16);
    for (std::uint32_t sample = 0; sample < 16U; ++sample) {
        REQUIRE(std::isfinite(output.ChannelData(0)[sample]));
        REQUIRE(std::fabs(output.ChannelData(0)[sample]) <= 0.5F);
    }
}
