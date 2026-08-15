#include "dsp/nam_processor.hpp"

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iterator>

namespace solo_rig {

namespace {

NamArchitecture DetectArchitecture(const std::filesystem::path& source) {
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        return NamArchitecture::Unknown;
    }
    const std::string header((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (header.find("\"version\": \"0.6") != std::string::npos ||
        header.find("\"version\":\"0.6") != std::string::npos || header.find("\"version\": \"0.7") != std::string::npos ||
        header.find("\"version\":\"0.7") != std::string::npos) {
        return NamArchitecture::A2;
    }
    return header.find("\"architecture\"") != std::string::npos ? NamArchitecture::A1 : NamArchitecture::Unknown;
}

} // namespace

NamProcessor::~NamProcessor() = default;

NamProcessor::NamProcessor() = default;

std::optional<NamModelLoadError> NamProcessor::LoadModel(const std::filesystem::path& source) {
    prepared_ = false;
    if (!std::filesystem::is_regular_file(source)) {
        return NamModelLoadError::MissingFile;
    }
    std::string extension = source.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (extension != ".nam" && extension != ".nam-a2") {
        return NamModelLoadError::InvalidExtension;
    }

    std::filesystem::path load_source = source;
    std::filesystem::path compatibility_copy;
    if (extension == ".nam-a2") {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        compatibility_copy = std::filesystem::temp_directory_path() /
            ("solorig-nam-a2-" + std::to_string(stamp) + ".nam");
        std::error_code copy_error;
        std::filesystem::copy_file(source, compatibility_copy,
                                   std::filesystem::copy_options::overwrite_existing, copy_error);
        if (copy_error) {
            return NamModelLoadError::LoadFailure;
        }
        load_source = compatibility_copy;
    }

    try {
        auto model = nam::get_dsp(load_source);
        std::error_code cleanup_error;
        if (!compatibility_copy.empty()) std::filesystem::remove(compatibility_copy, cleanup_error);
        if (model == nullptr) {
            return NamModelLoadError::UnsupportedModel;
        }
        const auto input_channels = model->NumInputChannels();
        const auto output_channels = model->NumOutputChannels();
        if (input_channels <= 0 || output_channels <= 0) {
            return NamModelLoadError::UnsupportedModel;
        }
        model_info_ = NamModelInfo{
            .source_path = source,
            .architecture = DetectArchitecture(source),
            .expected_sample_rate_hz = model->GetExpectedSampleRate(),
            .input_channels = static_cast<std::uint32_t>(input_channels),
            .output_channels = static_cast<std::uint32_t>(output_channels),
        };
        model_ = std::move(model);
        input_channels_.clear();
        output_channels_.clear();
        return std::nullopt;
    } catch (...) {
        std::error_code cleanup_error;
        if (!compatibility_copy.empty()) std::filesystem::remove(compatibility_copy, cleanup_error);
        model_.reset();
        model_info_.reset();
        return NamModelLoadError::LoadFailure;
    }
}

std::optional<NamModelInfo> NamProcessor::ModelInfo() const {
    return model_info_;
}

bool NamProcessor::Prepare(const AudioFormat& format) {
    prepared_ = false;
    if (model_ == nullptr || !model_info_.has_value() || !format.Validate().empty() ||
        format.channel_count != model_info_->input_channels || format.channel_count != model_info_->output_channels) {
        return false;
    }
    try {
        model_->Reset(format.sample_rate_hz, static_cast<int>(format.max_block_size));
    } catch (...) {
        return false;
    }
    input_channels_.assign(format.channel_count, nullptr);
    output_channels_.assign(format.channel_count, nullptr);
    prepared_ = true;
    return true;
}

void NamProcessor::Process(const AudioBuffer& input,
                           AudioBuffer& output,
                           std::uint32_t sample_count) noexcept {
    if (!prepared_ || model_ == nullptr || sample_count == 0 || sample_count > input.Capacity() ||
        sample_count > output.Capacity() || input.ChannelCount() != input_channels_.size() ||
        output.ChannelCount() != output_channels_.size()) {
        return;
    }
    for (std::uint32_t channel = 0; channel < input.ChannelCount(); ++channel) {
        input_channels_[channel] = const_cast<float*>(input.ChannelData(channel));
        output_channels_[channel] = output.ChannelData(channel);
    }
    try {
        model_->process(input_channels_.data(), output_channels_.data(), static_cast<int>(sample_count));
    } catch (...) {
        output.Clear(sample_count);
    }
}

std::uint32_t NamProcessor::DeclaredLatencySamples() const noexcept {
    return 0;
}

} // namespace solo_rig
