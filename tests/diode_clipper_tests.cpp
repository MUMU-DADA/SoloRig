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
    REQUIRE(!clipper.SetSettings({.drive = 1.0F,
                                  .output_gain = 1.0F,
                                  .diode_threshold = 0.35F,
                                  .input_resistance_ohms = 0.0F}));
}

TEST_CASE("DiodeClipper models capacitor history with a stable MNA circuit solve") {
    solo_rig::DiodeClipper clipper({.drive = 1.0F,
                                    .output_gain = 1.0F,
                                    .diode_threshold = 10.0F,
                                    .input_resistance_ohms = 1000.0F,
                                    .filter_capacitance_farads = 0.00001F});
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    input.ChannelData(0)[0] = 1.0F;
    input.ChannelData(0)[1] = 1.0F;

    REQUIRE(clipper.Prepare(CreateFormat()));
    clipper.Process(input, output, 2);
    REQUIRE(output.ChannelData(0)[0] > 0.0F);
    REQUIRE(output.ChannelData(0)[1] > output.ChannelData(0)[0]);
}

TEST_CASE("DiodeClipper keeps extreme driven samples finite during Newton iteration") {
    solo_rig::DiodeClipper clipper({.drive = 1000.0F,
                                    .output_gain = 1.0F,
                                    .diode_threshold = 0.35F,
                                    .input_resistance_ohms = 4700.0F,
                                    .newton_iterations = 8});
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    input.ChannelData(0)[0] = 1000.0F;

    REQUIRE(clipper.Prepare(CreateFormat()));
    clipper.Process(input, output, 1);
    REQUIRE(std::isfinite(output.ChannelData(0)[0]));
}
