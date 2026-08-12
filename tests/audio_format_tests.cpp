#include "core/audio_format.hpp"
#include "test_framework.hpp"

using solo_rig::AudioFormat;
using solo_rig::SampleRepresentation;

TEST_CASE("AudioFormat accepts the required 192 kHz Float32 stereo format") {
    const AudioFormat format{
        .sample_rate_hz = 192000.0,
        .max_block_size = 64,
        .channel_count = 2,
        .sample_representation = SampleRepresentation::Float32,
    };

    REQUIRE(format.Validate().empty());
}

TEST_CASE("AudioFormat accepts a VST3 host format below 192 kHz") {
    const AudioFormat format{
        .sample_rate_hz = 48000.0,
        .max_block_size = 128,
        .channel_count = 2,
        .sample_representation = SampleRepresentation::Float32,
    };

    REQUIRE(format.Validate().empty());
}

TEST_CASE("AudioFormat rejects invalid real-time processing values") {
    const AudioFormat invalid_sample_rate{
        .sample_rate_hz = 0.0,
        .max_block_size = 64,
        .channel_count = 2,
        .sample_representation = SampleRepresentation::Float32,
    };
    const AudioFormat invalid_block_size{
        .sample_rate_hz = 48000.0,
        .max_block_size = 0,
        .channel_count = 2,
        .sample_representation = SampleRepresentation::Float32,
    };
    const AudioFormat invalid_channel_count{
        .sample_rate_hz = 48000.0,
        .max_block_size = 64,
        .channel_count = 0,
        .sample_representation = SampleRepresentation::Float32,
    };

    REQUIRE(!invalid_sample_rate.Validate().empty());
    REQUIRE(!invalid_block_size.Validate().empty());
    REQUIRE(!invalid_channel_count.Validate().empty());
}
