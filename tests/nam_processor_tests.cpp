#include "core/audio_buffer.hpp"
#include "dsp/nam_processor.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <filesystem>

namespace {

std::filesystem::path ExampleModel(std::string_view name) {
    return std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "third_party" / "NeuralAmpModelerCore" / "example_models" /
           std::string(name);
}

solo_rig::AudioFormat NamFormat() {
    return {.sample_rate_hz = 48000.0, .max_block_size = 16, .channel_count = 1};
}

} // namespace

TEST_CASE("NamProcessor loads and processes a legacy A1 NAM model") {
    solo_rig::NamProcessor processor;
    solo_rig::AudioBuffer input(1, 16);
    solo_rig::AudioBuffer output(1, 16);
    input.ChannelData(0)[0] = 0.1F;

    const auto model_path = ExampleModel("wavenet.nam");
    REQUIRE(std::filesystem::is_regular_file(model_path));
    REQUIRE(!processor.LoadModel(model_path).has_value());
    REQUIRE(processor.ModelInfo().has_value());
    REQUIRE(processor.ModelInfo()->architecture == solo_rig::NamArchitecture::A1);
    REQUIRE(processor.Prepare(NamFormat()));
    processor.Process(input, output, 16);
    REQUIRE(std::isfinite(output.ChannelData(0)[0]));
}

TEST_CASE("NamProcessor loads and processes the A2 NAM reference model") {
    solo_rig::NamProcessor processor;
    solo_rig::AudioBuffer input(1, 16);
    solo_rig::AudioBuffer output(1, 16);
    input.ChannelData(0)[0] = 0.1F;

    const auto model_path = ExampleModel("wavenet_a2_max.nam");
    REQUIRE(std::filesystem::is_regular_file(model_path));
    REQUIRE(!processor.LoadModel(model_path).has_value());
    REQUIRE(processor.ModelInfo().has_value());
    REQUIRE(processor.ModelInfo()->architecture == solo_rig::NamArchitecture::A2);
    REQUIRE(processor.Prepare(NamFormat()));
    processor.Process(input, output, 16);
    REQUIRE(std::isfinite(output.ChannelData(0)[0]));

    const auto renamed_path = std::filesystem::temp_directory_path() / "solorig-a2-model.nam-a2";
    std::error_code cleanup;
    std::filesystem::copy_file(model_path, renamed_path,
                               std::filesystem::copy_options::overwrite_existing, cleanup);
    REQUIRE(!cleanup);
    REQUIRE(!processor.LoadModel(renamed_path).has_value());
    REQUIRE(processor.ModelInfo().has_value());
    REQUIRE(processor.ModelInfo()->architecture == solo_rig::NamArchitecture::A2);
    std::filesystem::remove(renamed_path, cleanup);
}

TEST_CASE("NamProcessor refuses a missing or non-NAM external model file") {
    solo_rig::NamProcessor processor;
    REQUIRE(processor.LoadModel("missing.nam").value() == solo_rig::NamModelLoadError::MissingFile);
    REQUIRE(processor.LoadModel(ExampleModel("README.md")).value() == solo_rig::NamModelLoadError::MissingFile);
}
