#pragma once

#include "core/audio_graph.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace solo_rig {

enum class ImpulseResponseLoadError {
    EmptyImpulseResponse,
    TooLong,
    NonFiniteSample,
};

class ImpulseResponseConvolver final : public EffectProcessor {
public:
    explicit ImpulseResponseConvolver(std::uint32_t maximum_ir_length = 8192);

    [[nodiscard]] std::vector<ImpulseResponseLoadError> LoadMonoImpulseResponse(std::span<const float> samples);

    bool Prepare(const AudioFormat& format) override;
    void Process(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept override;
    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override;
    [[nodiscard]] std::uint32_t ImpulseResponseLength() const noexcept;

private:
    std::uint32_t maximum_ir_length_;
    std::vector<float> impulse_response_;
    std::vector<float> history_;
    std::uint32_t history_position_{};
    std::uint32_t channel_count_{};
};

} // namespace solo_rig
