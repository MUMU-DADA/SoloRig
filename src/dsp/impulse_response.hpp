#pragma once

#include "core/audio_graph.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <span>
#include <variant>
#include <vector>

namespace solo_rig {

enum class ImpulseResponseLoadError {
    EmptyImpulseResponse,
    TooLong,
    NonFiniteSample,
    InvalidSampleRate,
};

enum class ImpulseResponseFileLoadError {
    MissingFile,
    CannotReadFile,
    InvalidWaveContainer,
    MissingFormatChunk,
    MissingDataChunk,
    UnsupportedEncoding,
    UnsupportedChannelCount,
    UnsupportedBitDepth,
    InvalidWaveData,
    TooLong,
    NonFiniteSample,
};

struct ImportedImpulseResponse {
    double sample_rate_hz{};
    std::vector<float> mono_samples;
};

// Decodes a RIFF/WAVE cabinet response outside the audio callback. PCM 16/24/32
// and IEEE Float32 are supported; multi-channel files are averaged to mono.
[[nodiscard]] std::variant<ImportedImpulseResponse, ImpulseResponseFileLoadError> LoadWaveImpulseResponse(
    const std::filesystem::path& source,
    std::uint32_t maximum_ir_length = 8192);

class ImpulseResponseConvolver final : public EffectProcessor {
public:
    explicit ImpulseResponseConvolver(std::uint32_t maximum_ir_length = 8192);

    [[nodiscard]] std::vector<ImpulseResponseLoadError> LoadMonoImpulseResponse(std::span<const float> samples);
    [[nodiscard]] std::vector<ImpulseResponseLoadError> LoadImportedImpulseResponse(
        const ImportedImpulseResponse& imported);

    bool Prepare(const AudioFormat& format) override;
    void Process(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept override;
    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override;
    [[nodiscard]] std::uint32_t ImpulseResponseLength() const noexcept;

private:
    std::uint32_t maximum_ir_length_;
    double source_sample_rate_hz_{};
    std::vector<float> source_impulse_response_;
    std::vector<float> impulse_response_;
    std::vector<float> history_;
    std::uint32_t history_position_{};
    std::uint32_t channel_count_{};
    bool prepared_{};
};

} // namespace solo_rig
