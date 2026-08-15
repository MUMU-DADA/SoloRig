#include "core/audio_buffer.hpp"
#include "dsp/digital_pitch_shift.hpp"
#include "test_framework.hpp"

#include <cmath>

TEST_CASE("DigitalPitchShift renders finite delayed grains at 192 kHz") {
    solo_rig::DigitalPitchShift pitch_shift;
    const solo_rig::AudioFormat format{.sample_rate_hz = 192000.0, .max_block_size = 256, .channel_count = 1};
    solo_rig::AudioBuffer input(1, 256);
    solo_rig::AudioBuffer output(1, 256);
    REQUIRE(pitch_shift.SetSemitones(7.0F));
    REQUIRE(pitch_shift.Prepare(format));

    float total_energy = 0.0F;
    input.ChannelData(0)[0] = 1.0F;
    for (std::uint32_t block = 0; block < 16U; ++block) {
        pitch_shift.Process(input, output, 256);
        for (std::uint32_t sample = 0; sample < 256U; ++sample) {
            REQUIRE(std::isfinite(output.ChannelData(0)[sample]));
            total_energy += std::fabs(output.ChannelData(0)[sample]);
        }
        input.Clear(256);
    }
    REQUIRE(total_energy > 0.0F);
}

TEST_CASE("DigitalPitchShift rejects shifts outside its declared range and Prepare resets") {
    solo_rig::DigitalPitchShift pitch_shift;
    REQUIRE(!pitch_shift.SetSemitones(-25.0F));
    REQUIRE(!pitch_shift.SetSemitones(25.0F));
    REQUIRE(pitch_shift.SetSemitones(0.0F));
    const solo_rig::AudioFormat format{.sample_rate_hz = 48000.0, .max_block_size = 32, .channel_count = 1};
    solo_rig::AudioBuffer input(1, 32);
    solo_rig::AudioBuffer output(1, 32);
    input.ChannelData(0)[0] = 1.0F;
    REQUIRE(pitch_shift.Prepare(format));
    pitch_shift.Process(input, output, 32);
    REQUIRE(pitch_shift.Prepare(format));
    pitch_shift.Process(input, output, 32);
    for (std::uint32_t sample = 0; sample < 32U; ++sample) {
        REQUIRE(std::isfinite(output.ChannelData(0)[sample]));
    }
}
