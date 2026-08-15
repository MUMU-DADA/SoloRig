#include "standalone/asio_sample_conversion.hpp"
#include "test_framework.hpp"

#include <array>
#include <cstdint>

TEST_CASE("ASIO Int16 and Int32 conversion preserves normalized signed endpoints") {
    const std::array<std::int16_t, 3> input16{-32768, 0, 32767};
    std::array<float, 3> converted{};
    REQUIRE(solo_rig::standalone::ConvertAsioInputToFloat32(input16.data(),
                                                            converted.data(),
                                                            solo_rig::standalone::asio_abi::kInt16Lsb,
                                                            static_cast<std::uint32_t>(converted.size())));
    REQUIRE(converted[0] == -1.0F);
    REQUIRE(converted[1] == 0.0F);
    REQUIRE(converted[2] > 0.9999F);

    const std::array<std::int32_t, 3> input32{INT32_MIN, 0, INT32_MAX};
    REQUIRE(solo_rig::standalone::ConvertAsioInputToFloat32(input32.data(),
                                                            converted.data(),
                                                            solo_rig::standalone::asio_abi::kInt32Lsb,
                                                            static_cast<std::uint32_t>(converted.size())));
    REQUIRE(converted[0] == -1.0F);
    REQUIRE(converted[1] == 0.0F);
    REQUIRE(converted[2] > 0.9999F);
}

TEST_CASE("ASIO Int24 conversion sign extends packed little-endian samples") {
    const std::array<std::uint8_t, 6> input{0x00U, 0x00U, 0x80U, 0xFFU, 0xFFU, 0x7FU};
    std::array<float, 2> converted{};
    REQUIRE(solo_rig::standalone::ConvertAsioInputToFloat32(input.data(),
                                                            converted.data(),
                                                            solo_rig::standalone::asio_abi::kInt24Lsb,
                                                            static_cast<std::uint32_t>(converted.size())));
    REQUIRE(converted[0] == -1.0F);
    REQUIRE(converted[1] > 0.9999F);
}

TEST_CASE("ASIO output conversion clamps and rejects unsupported representations") {
    const std::array<float, 3> input{-2.0F, 0.0F, 2.0F};
    std::array<std::int16_t, 3> output{};
    REQUIRE(solo_rig::standalone::ConvertFloat32ToAsioOutput(input.data(),
                                                             output.data(),
                                                             solo_rig::standalone::asio_abi::kInt16Lsb,
                                                             static_cast<std::uint32_t>(output.size())));
    REQUIRE(output[0] == -32767);
    REQUIRE(output[1] == 0);
    REQUIRE(output[2] == 32767);
    REQUIRE(!solo_rig::standalone::ConvertFloat32ToAsioOutput(input.data(),
                                                              output.data(),
                                                              solo_rig::standalone::asio_abi::kFloat32Lsb,
                                                              static_cast<std::uint32_t>(output.size())));
}
