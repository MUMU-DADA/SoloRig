#include "core/audio_buffer.hpp"
#include "dsp/digital_dynamics.hpp"
#include "test_framework.hpp"

#include <cmath>

TEST_CASE("DigitalDynamics amount zero is transparent and higher amount compresses") {
    const solo_rig::AudioFormat format{.sample_rate_hz = 192000.0, .max_block_size = 8, .channel_count = 1};
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer transparent(1, 8);
    solo_rig::AudioBuffer compressed(1, 8);
    input.ChannelData(0)[0] = 2.0F;

    solo_rig::DigitalDynamics no_dynamics;
    REQUIRE(no_dynamics.Prepare(format));
    no_dynamics.Process(input, transparent, 1);
    REQUIRE(transparent.ChannelData(0)[0] == 2.0F);

    solo_rig::DigitalDynamics limiter;
    REQUIRE(limiter.SetAmount(1.0F));
    REQUIRE(limiter.Prepare(format));
    limiter.Process(input, compressed, 1);
    REQUIRE(std::isfinite(compressed.ChannelData(0)[0]));
    REQUIRE(std::fabs(compressed.ChannelData(0)[0]) < std::fabs(input.ChannelData(0)[0]));
}

TEST_CASE("DigitalDynamics validates amount and restores deterministic state on Prepare") {
    solo_rig::DigitalDynamics dynamics;
    REQUIRE(!dynamics.SetAmount(-0.01F));
    REQUIRE(!dynamics.SetAmount(1.01F));
    REQUIRE(dynamics.SetAmount(0.5F));
    const solo_rig::AudioFormat format{.sample_rate_hz = 48000.0, .max_block_size = 4, .channel_count = 1};
    solo_rig::AudioBuffer input(1, 4);
    solo_rig::AudioBuffer output(1, 4);
    input.ChannelData(0)[0] = 1.5F;
    REQUIRE(dynamics.Prepare(format));
    dynamics.Process(input, output, 1);
    const float first = output.ChannelData(0)[0];
    REQUIRE(dynamics.Prepare(format));
    dynamics.Process(input, output, 1);
    REQUIRE(std::fabs(output.ChannelData(0)[0] - first) < 1.0e-6F);
}
