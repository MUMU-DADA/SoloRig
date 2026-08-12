#include "core/audio_buffer.hpp"
#include "dsp/impulse_response.hpp"
#include "test_framework.hpp"

#include <array>
#include <cmath>

TEST_CASE("ImpulseResponseConvolver applies a loaded cabinet impulse response") {
    solo_rig::ImpulseResponseConvolver convolver;
    const std::array<float, 2> ir{1.0F, 0.5F};
    const solo_rig::AudioFormat format{.sample_rate_hz = 48000.0, .max_block_size = 8, .channel_count = 1};
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    input.ChannelData(0)[0] = 1.0F;

    REQUIRE(convolver.LoadMonoImpulseResponse(ir).empty());
    REQUIRE(convolver.Prepare(format));
    convolver.Process(input, output, 3);
    REQUIRE(output.ChannelData(0)[0] == 1.0F);
    REQUIRE(output.ChannelData(0)[1] == 0.5F);
    REQUIRE(output.ChannelData(0)[2] == 0.0F);
}

TEST_CASE("ImpulseResponseConvolver rejects empty or non-finite imported IR data") {
    solo_rig::ImpulseResponseConvolver convolver;
    const std::array<float, 0> empty{};
    const std::array<float, 1> invalid{std::nanf("")};

    REQUIRE(!convolver.LoadMonoImpulseResponse(empty).empty());
    REQUIRE(!convolver.LoadMonoImpulseResponse(invalid).empty());
}
