#include "core/audio_buffer.hpp"
#include "test_framework.hpp"

using solo_rig::AudioBuffer;

TEST_CASE("AudioBuffer owns preallocated channel-major Float32 samples") {
    AudioBuffer buffer(2, 8);

    REQUIRE(buffer.ChannelCount() == 2);
    REQUIRE(buffer.Capacity() == 8);
    REQUIRE(buffer.IsValid());

    buffer.ChannelData(0)[0] = 0.25F;
    buffer.ChannelData(1)[0] = -0.5F;

    REQUIRE(buffer.ChannelData(0)[0] == 0.25F);
    REQUIRE(buffer.ChannelData(1)[0] == -0.5F);
}

TEST_CASE("AudioBuffer clear and copy only affect the requested audio block") {
    AudioBuffer source(1, 8);
    AudioBuffer destination(1, 8);

    source.ChannelData(0)[0] = 0.1F;
    source.ChannelData(0)[1] = 0.2F;
    source.ChannelData(0)[2] = 0.3F;
    destination.ChannelData(0)[3] = 0.9F;

    REQUIRE(destination.CopyFrom(source, 3));
    REQUIRE(destination.ChannelData(0)[0] == 0.1F);
    REQUIRE(destination.ChannelData(0)[2] == 0.3F);
    REQUIRE(destination.ChannelData(0)[3] == 0.9F);

    destination.Clear(3);
    REQUIRE(destination.ChannelData(0)[0] == 0.0F);
    REQUIRE(destination.ChannelData(0)[2] == 0.0F);
    REQUIRE(destination.ChannelData(0)[3] == 0.9F);
}

TEST_CASE("AudioBuffer rejects incompatible copy and mix requests") {
    AudioBuffer mono(1, 8);
    AudioBuffer stereo(2, 8);

    REQUIRE(!mono.CopyFrom(stereo, 4));
    REQUIRE(!mono.AddFrom(stereo, 4));
    REQUIRE(!mono.CopyFrom(mono, 9));
}
