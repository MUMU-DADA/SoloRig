#include "core/audio_buffer.hpp"
#include "dsp/digital_filter.hpp"
#include "test_framework.hpp"

#include <cmath>

TEST_CASE("DigitalFilter low-pass response is finite, causal and stateful") {
    solo_rig::DigitalFilter filter;
    const solo_rig::AudioFormat format{.sample_rate_hz = 48000.0, .max_block_size = 8, .channel_count = 1};
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        input.ChannelData(0)[sample] = 1.0F;
    }

    REQUIRE(filter.SetCutoffHz(1000.0F));
    REQUIRE(filter.Prepare(format));
    filter.Process(input, output, 8);
    REQUIRE(output.ChannelData(0)[0] > 0.0F);
    REQUIRE(output.ChannelData(0)[0] < 1.0F);
    REQUIRE(output.ChannelData(0)[7] > output.ChannelData(0)[0]);
    for (std::uint32_t sample = 0; sample < 8U; ++sample) {
        REQUIRE(std::isfinite(output.ChannelData(0)[sample]));
    }
}

TEST_CASE("DigitalFilter rejects unsafe cutoff and Prepare resets history") {
    solo_rig::DigitalFilter filter;
    const solo_rig::AudioFormat format{.sample_rate_hz = 48000.0, .max_block_size = 4, .channel_count = 1};
    solo_rig::AudioBuffer input(1, 4);
    solo_rig::AudioBuffer output(1, 4);
    input.ChannelData(0)[0] = 1.0F;
    REQUIRE(filter.SetCutoffHz(24000.0F));
    REQUIRE(!filter.Prepare(format));
    REQUIRE(filter.SetCutoffHz(2000.0F));
    REQUIRE(filter.Prepare(format));
    filter.Process(input, output, 1);
    const float first = output.ChannelData(0)[0];
    REQUIRE(filter.Prepare(format));
    filter.Process(input, output, 1);
    REQUIRE(std::fabs(output.ChannelData(0)[0] - first) < 1.0e-6F);
    REQUIRE(!filter.SetCutoffHz(24000.0F));
}
