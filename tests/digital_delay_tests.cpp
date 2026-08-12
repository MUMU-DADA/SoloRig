#include "core/audio_buffer.hpp"
#include "dsp/digital_delay.hpp"
#include "test_framework.hpp"

TEST_CASE("DigitalDelay emits a delayed impulse at the configured sample time") {
    solo_rig::DigitalDelay delay(0.1F, {.delay_seconds = 2.0F / 48000.0F, .feedback = 0.0F, .wet = 1.0F, .dry = 0.0F});
    const solo_rig::AudioFormat format{.sample_rate_hz = 48000.0, .max_block_size = 8, .channel_count = 1};
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    input.ChannelData(0)[0] = 1.0F;

    REQUIRE(delay.Prepare(format));
    delay.Process(input, output, 4);
    REQUIRE(output.ChannelData(0)[0] == 0.0F);
    REQUIRE(output.ChannelData(0)[1] == 0.0F);
    REQUIRE(output.ChannelData(0)[2] == 1.0F);
}

TEST_CASE("DigitalDelay rejects settings that would become unstable") {
    solo_rig::DigitalDelay delay;
    REQUIRE(!delay.SetSettings({.delay_seconds = 0.3F, .feedback = 1.0F, .wet = 0.5F, .dry = 1.0F}));
    REQUIRE(!delay.SetSettings({.delay_seconds = 3.0F, .feedback = 0.0F, .wet = 0.5F, .dry = 1.0F}));
}
