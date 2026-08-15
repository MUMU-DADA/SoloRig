#include "standalone/audio_resampler.hpp"
#include "test_framework.hpp"

#include <array>

TEST_CASE("Standalone linear resampler calculates deterministic internal block sizes") {
    REQUIRE(solo_rig::ResampledBlockSize(64U, 48000.0, 192000.0) == 256U);
    REQUIRE(solo_rig::ResampledBlockSize(64U, 44100.0, 192000.0) == 279U);
    REQUIRE(solo_rig::ResampledBlockSize(256U, 192000.0, 48000.0) == 64U);
    REQUIRE(solo_rig::ResampledBlockSize(0U, 48000.0, 192000.0) == 0U);
}

TEST_CASE("Standalone linear resampler preserves endpoints and rejects invalid rates") {
    const std::array<float, 2> source{0.0F, 1.0F};
    std::array<float, 5> upsampled{};
    REQUIRE(solo_rig::ResampleLinear(source, upsampled, 1.0, 4.0));
    REQUIRE(upsampled.front() == 0.0F);
    REQUIRE(upsampled.back() == 1.0F);
    REQUIRE(upsampled.at(2) > 0.4F);
    REQUIRE(upsampled.at(2) < 0.6F);

    std::array<float, 2> invalid_destination{};
    REQUIRE(!solo_rig::ResampleLinear(source, invalid_destination, 0.0, 192000.0));
}
