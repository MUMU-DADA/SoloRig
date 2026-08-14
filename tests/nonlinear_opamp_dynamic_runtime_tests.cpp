#include "content/circuit_v2.hpp"
#include "content/module_package.hpp"
#include "content/module_runtime.hpp"
#include "content/model_database.hpp"
#include "core/audio_buffer.hpp"
#include "dsp/mna_solver.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>
#include <utility>
#include <variant>

namespace {
std::string ReadFixture(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" / "whitebox" / name;
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::filesystem::path CreatePackage() {
    const auto directory = std::filesystem::temp_directory_path() / "SoloRigC9eRuntimeTest";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory / "logic", error);
    std::ofstream(directory / "manifest.json", std::ios::binary | std::ios::trunc) << R"({
      "schemaVersion":1,"moduleId":"org.mumu-dada.solorig.c9e-dynamic-opamp",
      "displayName":"C9e Dynamic Opamp","category":"analog-circuit","logicFile":"logic/main.json",
      "ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}]})";
    std::ofstream(directory / "logic" / "main.json", std::ios::binary | std::ios::trunc) << R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"a","type":"opamp","pins":["out","0","in","0"],"model":"solorig.cleanroom.opamp.dynamic.v1"},
                    {"id":"load","type":"resistor","pins":["out","0"],"value":1000}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4,
        "convergence":{"maxIterations":64,"absoluteTolerance":1e-10,"relativeTolerance":1e-8}}})";
    return directory;
}
}

TEST_CASE("M18-C9e dynamic opamp runtime remains preallocated at 192 kHz 4x") {
    const auto parsed = solo_rig::ParseCircuitV2Json(ReadFixture("m18c9e_dynamic_opamp_reference.json"));
    REQUIRE(parsed.circuit.has_value());
    auto compiled = solo_rig::CompileMnaCircuit(*parsed.circuit);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(192000.0));
    constexpr auto contract = solo_rig::MnaPreparedCircuit::ProcessContract();
    static_assert(!contract.process_allocates && !contract.process_locks &&
                  !contract.process_reads_files && !contract.process_parses_json);
    for (std::uint32_t sample = 0; sample < 1024U; ++sample) {
        REQUIRE(compiled.circuit->Solve(sample % 2U == 0U ? -1.0 : 1.0).Succeeded());
    }
}

TEST_CASE("M18-C9e external v2 runtime binds the audited dynamic opamp before Process") {
    const auto directory = CreatePackage();
    const auto package = solo_rig::LoadModulePackage(directory);
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    const auto database = solo_rig::BuildModelDatabase(ReadFixture("m18c9e_cleanroom_dynamic_opamp_model.json"));
    REQUIRE(package.package.has_value());
    REQUIRE(database.database.has_value());
    REQUIRE(std::holds_alternative<solo_rig::ModuleRuntimeError>(solo_rig::CreateModuleRuntime(*package.package)));
    auto runtime = solo_rig::CreateModuleRuntime(*package.package, *database.database);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    auto processor = std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(runtime));
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) input.ChannelData(0)[sample] = 0.25F;
    REQUIRE(processor->Prepare({.sample_rate_hz = 192000.0, .max_block_size = 8, .channel_count = 1}));
    processor->Process(input, output, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        REQUIRE(std::isfinite(output.ChannelData(0)[sample]));
        REQUIRE(output.ChannelData(0)[sample] > 0.0F);
        REQUIRE(output.ChannelData(0)[sample] <= 10.0F);
    }
}
