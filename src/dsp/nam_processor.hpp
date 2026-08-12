#pragma once

#include "core/audio_graph.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nam {
class DSP;
}

namespace solo_rig {

enum class NamArchitecture {
    A1,
    A2,
    Unknown,
};

struct NamModelInfo {
    std::filesystem::path source_path;
    NamArchitecture architecture{NamArchitecture::Unknown};
    double expected_sample_rate_hz{};
    std::uint32_t input_channels{};
    std::uint32_t output_channels{};
};

enum class NamModelLoadError {
    MissingFile,
    InvalidExtension,
    UnsupportedModel,
    LoadFailure,
};

class NamProcessor final : public EffectProcessor {
public:
    NamProcessor();
    ~NamProcessor() override;

    NamProcessor(const NamProcessor&) = delete;
    NamProcessor& operator=(const NamProcessor&) = delete;

    [[nodiscard]] std::optional<NamModelLoadError> LoadModel(const std::filesystem::path& source);
    [[nodiscard]] std::optional<NamModelInfo> ModelInfo() const;

    bool Prepare(const AudioFormat& format) override;
    void Process(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept override;
    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override;

private:
    std::unique_ptr<nam::DSP> model_;
    std::optional<NamModelInfo> model_info_;
    std::vector<float*> input_channels_;
    std::vector<float*> output_channels_;
};

} // namespace solo_rig
