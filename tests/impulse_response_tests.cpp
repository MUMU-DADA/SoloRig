#include "core/audio_buffer.hpp"
#include "dsp/impulse_response.hpp"
#include "test_framework.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <variant>
#include <vector>

namespace {

void AppendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::uint32_t index = 0; index < 4U; ++index) {
        bytes.push_back(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU));
    }
}

std::filesystem::path WritePcm16Wave(std::uint16_t channel_count, const std::vector<std::int16_t>& interleaved_samples) {
    const auto path = std::filesystem::temp_directory_path() / "SoloRigImportedIrTest.wav";
    std::vector<std::uint8_t> bytes;
    const auto data_size = static_cast<std::uint32_t>(interleaved_samples.size() * sizeof(std::int16_t));
    bytes.insert(bytes.end(), {'R', 'I', 'F', 'F'});
    AppendU32(bytes, 36U + data_size);
    bytes.insert(bytes.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    AppendU32(bytes, 16U);
    AppendU16(bytes, 1U);
    AppendU16(bytes, channel_count);
    AppendU32(bytes, 48000U);
    AppendU32(bytes, 48000U * channel_count * 2U);
    AppendU16(bytes, static_cast<std::uint16_t>(channel_count * 2U));
    AppendU16(bytes, 16U);
    bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
    AppendU32(bytes, data_size);
    for (const auto sample : interleaved_samples) {
        AppendU16(bytes, static_cast<std::uint16_t>(sample));
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
}

std::filesystem::path WriteFloat32Wave(const std::vector<float>& samples) {
    const auto path = std::filesystem::temp_directory_path() / "SoloRigFloatImportedIrTest.wav";
    std::vector<std::uint8_t> bytes;
    const auto data_size = static_cast<std::uint32_t>(samples.size() * sizeof(float));
    bytes.insert(bytes.end(), {'R', 'I', 'F', 'F'});
    AppendU32(bytes, 36U + data_size);
    bytes.insert(bytes.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    AppendU32(bytes, 16U);
    AppendU16(bytes, 3U);
    AppendU16(bytes, 1U);
    AppendU32(bytes, 48000U);
    AppendU32(bytes, 48000U * 4U);
    AppendU16(bytes, 4U);
    AppendU16(bytes, 32U);
    bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
    AppendU32(bytes, data_size);
    for (const auto sample : samples) {
        AppendU32(bytes, std::bit_cast<std::uint32_t>(sample));
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
}

} // namespace

TEST_CASE("ImpulseResponseConvolver applies a loaded cabinet impulse response") {
    solo_rig::ImpulseResponseConvolver convolver;
    const std::array<float, 2> ir{1.0F, 0.5F};
    const solo_rig::AudioFormat format{.sample_rate_hz = 48000.0, .max_block_size = 8, .channel_count = 1};
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    input.ChannelData(0)[0] = 1.0F;

    REQUIRE(convolver.LoadMonoImpulseResponse(ir).empty());
    REQUIRE(convolver.Prepare(format));
    convolver.Process(input, output, 3);
    REQUIRE(output.ChannelData(0)[0] == 1.0F);
    REQUIRE(output.ChannelData(0)[1] == 0.5F);
    REQUIRE(output.ChannelData(0)[2] == 0.0F);
}

TEST_CASE("ImpulseResponseConvolver rejects empty or non-finite imported IR data") {
    solo_rig::ImpulseResponseConvolver convolver;
    const std::array<float, 0> empty{};
    const std::array<float, 1> invalid{std::nanf("")};

    REQUIRE(!convolver.LoadMonoImpulseResponse(empty).empty());
    REQUIRE(!convolver.LoadMonoImpulseResponse(invalid).empty());
}

TEST_CASE("Wave IR import decodes PCM16, downmixes channels and feeds the cabinet convolver") {
    const auto path = WritePcm16Wave(2U, {32767, 0, 16384, 16384});
    const auto imported = solo_rig::LoadWaveImpulseResponse(path, 8U);
    std::error_code error;
    std::filesystem::remove(path, error);

    REQUIRE(std::holds_alternative<solo_rig::ImportedImpulseResponse>(imported));
    const auto& impulse = std::get<solo_rig::ImportedImpulseResponse>(imported);
    REQUIRE(impulse.sample_rate_hz == 48000.0);
    REQUIRE(impulse.mono_samples.size() == 2U);
    REQUIRE(std::fabs(impulse.mono_samples[0] - 0.5F) < 0.0001F);
    REQUIRE(std::fabs(impulse.mono_samples[1] - 0.5F) < 0.0001F);

    solo_rig::ImpulseResponseConvolver convolver;
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    input.ChannelData(0)[0] = 1.0F;
    REQUIRE(convolver.LoadMonoImpulseResponse(impulse.mono_samples).empty());
    REQUIRE(convolver.Prepare({.sample_rate_hz = 48000.0, .max_block_size = 8, .channel_count = 1}));
    convolver.Process(input, output, 2);
    REQUIRE(std::fabs(output.ChannelData(0)[0] - 0.5F) < 0.0001F);
    REQUIRE(std::fabs(output.ChannelData(0)[1] - 0.5F) < 0.0001F);
}

TEST_CASE("Wave IR import decodes IEEE Float32 samples without changing amplitudes") {
    const auto path = WriteFloat32Wave({0.25F, -0.5F});
    const auto imported = solo_rig::LoadWaveImpulseResponse(path, 8U);
    std::error_code error;
    std::filesystem::remove(path, error);

    REQUIRE(std::holds_alternative<solo_rig::ImportedImpulseResponse>(imported));
    const auto& impulse = std::get<solo_rig::ImportedImpulseResponse>(imported);
    REQUIRE(impulse.mono_samples.size() == 2U);
    REQUIRE(impulse.mono_samples[0] == 0.25F);
    REQUIRE(impulse.mono_samples[1] == -0.5F);
}

TEST_CASE("Wave IR import rejects malformed, unsupported and oversized external files") {
    const auto malformed = std::filesystem::temp_directory_path() / "SoloRigMalformedIrTest.wav";
    {
        std::ofstream output(malformed, std::ios::binary | std::ios::trunc);
        output << "not a wave";
    }
    const auto malformed_result = solo_rig::LoadWaveImpulseResponse(malformed, 8U);
    std::error_code error;
    std::filesystem::remove(malformed, error);

    const auto oversized_path = WritePcm16Wave(1U, {1, 2, 3});
    const auto oversized_result = solo_rig::LoadWaveImpulseResponse(oversized_path, 2U);
    std::filesystem::remove(oversized_path, error);

    REQUIRE(std::holds_alternative<solo_rig::ImpulseResponseFileLoadError>(malformed_result));
    REQUIRE(std::get<solo_rig::ImpulseResponseFileLoadError>(malformed_result) ==
            solo_rig::ImpulseResponseFileLoadError::InvalidWaveContainer);
    REQUIRE(std::holds_alternative<solo_rig::ImpulseResponseFileLoadError>(oversized_result));
    REQUIRE(std::get<solo_rig::ImpulseResponseFileLoadError>(oversized_result) ==
            solo_rig::ImpulseResponseFileLoadError::TooLong);
}
