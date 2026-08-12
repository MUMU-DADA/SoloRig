#include "core/audio_buffer.hpp"
#include "dsp/diode_clipper.hpp"
#include "test_framework.hpp"

#include <cmath>

namespace {

solo_rig::AudioFormat CreateFormat() {
    return {.sample_rate_hz = 48000.0, .max_block_size = 8, .channel_count = 1};
}

} // namespace

TEST_CASE("DiodeClipper preserves silence and has no algorithmic latency") {
    solo_rig::DiodeClipper clipper;
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);

    REQUIRE(clipper.Prepare(CreateFormat()));
    clipper.Process(input, output, 8);
    REQUIRE(output.ChannelData(0)[0] == 0.0F);
    REQUIRE(clipper.DeclaredLatencySamples() == 0);
}

TEST_CASE("DiodeClipper soft clips high input amplitudes") {
    solo_rig::DiodeClipper clipper({.drive = 4.0F, .output_gain = 1.0F, .diode_threshold = 0.35F});
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    input.ChannelData(0)[0] = 1.0F;

    REQUIRE(clipper.Prepare(CreateFormat()));
    clipper.Process(input, output, 1);
    REQUIRE(output.ChannelData(0)[0] > 0.35F);
    REQUIRE(output.ChannelData(0)[0] < 0.71F);
}

TEST_CASE("DiodeClipper rejects invalid circuit component settings") {
    solo_rig::DiodeClipper clipper;
    REQUIRE(!clipper.SetSettings({.drive = -1.0F, .output_gain = 1.0F, .diode_threshold = 0.35F}));
    REQUIRE(!clipper.SetSettings({.drive = 1.0F, .output_gain = 1.0F, .diode_threshold = 0.0F}));
}
