#include "dsp/impulse_response.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>

namespace solo_rig {

namespace {

bool HasFourCc(const std::vector<std::uint8_t>& data, std::size_t offset, const char (&identifier)[5]) {
    return offset + 4U <= data.size() && data[offset] == static_cast<std::uint8_t>(identifier[0]) &&
           data[offset + 1U] == static_cast<std::uint8_t>(identifier[1]) &&
           data[offset + 2U] == static_cast<std::uint8_t>(identifier[2]) &&
           data[offset + 3U] == static_cast<std::uint8_t>(identifier[3]);
}

std::uint16_t ReadU16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset]) | (static_cast<std::uint16_t>(data[offset + 1U]) << 8U);
}

std::uint32_t ReadU32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) | (static_cast<std::uint32_t>(data[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(data[offset + 2U]) << 16U) | (static_cast<std::uint32_t>(data[offset + 3U]) << 24U);
}

} // namespace

std::variant<ImportedImpulseResponse, ImpulseResponseFileLoadError> LoadWaveImpulseResponse(
    const std::filesystem::path& source,
    std::uint32_t maximum_ir_length) {
    if (!std::filesystem::is_regular_file(source)) {
        return ImpulseResponseFileLoadError::MissingFile;
    }
    std::ifstream stream(source, std::ios::binary);
    if (!stream) {
        return ImpulseResponseFileLoadError::CannotReadFile;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (bytes.size() < 12U || !HasFourCc(bytes, 0U, "RIFF") || !HasFourCc(bytes, 8U, "WAVE")) {
        return ImpulseResponseFileLoadError::InvalidWaveContainer;
    }

    std::uint16_t encoding{};
    std::uint16_t channel_count{};
    std::uint32_t sample_rate{};
    std::uint16_t block_align{};
    std::uint16_t bit_depth{};
    std::size_t data_offset{};
    std::size_t data_size{};
    bool found_format{};
    bool found_data{};
    std::size_t cursor = 12U;
    while (cursor + 8U <= bytes.size()) {
        const auto chunk_size = static_cast<std::size_t>(ReadU32(bytes, cursor + 4U));
        const auto chunk_data = cursor + 8U;
        if (chunk_size > bytes.size() - chunk_data) {
            return ImpulseResponseFileLoadError::InvalidWaveData;
        }
        if (HasFourCc(bytes, cursor, "fmt ")) {
            if (chunk_size < 16U || found_format) {
                return ImpulseResponseFileLoadError::InvalidWaveData;
            }
            encoding = ReadU16(bytes, chunk_data);
            channel_count = ReadU16(bytes, chunk_data + 2U);
            sample_rate = ReadU32(bytes, chunk_data + 4U);
            block_align = ReadU16(bytes, chunk_data + 12U);
            bit_depth = ReadU16(bytes, chunk_data + 14U);
            found_format = true;
        } else if (HasFourCc(bytes, cursor, "data") && !found_data) {
            data_offset = chunk_data;
            data_size = chunk_size;
            found_data = true;
        }
        const auto padded_size = chunk_size + (chunk_size & 1U);
        if (padded_size > bytes.size() - chunk_data) {
            return ImpulseResponseFileLoadError::InvalidWaveData;
        }
        cursor = chunk_data + padded_size;
    }
    if (!found_format) {
        return ImpulseResponseFileLoadError::MissingFormatChunk;
    }
    if (!found_data) {
        return ImpulseResponseFileLoadError::MissingDataChunk;
    }
    if (channel_count == 0U || channel_count > 8U) {
        return ImpulseResponseFileLoadError::UnsupportedChannelCount;
    }
    const bool pcm = encoding == 1U;
    const bool float32 = encoding == 3U && bit_depth == 32U;
    if (!pcm && !float32) {
        return ImpulseResponseFileLoadError::UnsupportedEncoding;
    }
    if ((!pcm || (bit_depth != 16U && bit_depth != 24U && bit_depth != 32U)) && !float32) {
        return ImpulseResponseFileLoadError::UnsupportedBitDepth;
    }
    const auto bytes_per_sample = static_cast<std::uint16_t>(bit_depth / 8U);
    if (sample_rate == 0U || bytes_per_sample == 0U || block_align != channel_count * bytes_per_sample ||
        data_size == 0U || data_size % block_align != 0U) {
        return ImpulseResponseFileLoadError::InvalidWaveData;
    }
    const auto frame_count = data_size / block_align;
    if (frame_count > maximum_ir_length) {
        return ImpulseResponseFileLoadError::TooLong;
    }

    ImportedImpulseResponse imported{.sample_rate_hz = static_cast<double>(sample_rate)};
    imported.mono_samples.reserve(frame_count);
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        float sum{};
        for (std::uint16_t channel = 0U; channel < channel_count; ++channel) {
            const auto offset = data_offset + frame * block_align + static_cast<std::size_t>(channel) * bytes_per_sample;
            float sample{};
            if (float32) {
                sample = std::bit_cast<float>(ReadU32(bytes, offset));
            } else if (bit_depth == 16U) {
                sample = static_cast<float>(static_cast<std::int16_t>(ReadU16(bytes, offset))) / 32768.0F;
            } else if (bit_depth == 24U) {
                std::int32_t raw = static_cast<std::int32_t>(bytes[offset]) |
                                   (static_cast<std::int32_t>(bytes[offset + 1U]) << 8U) |
                                   (static_cast<std::int32_t>(bytes[offset + 2U]) << 16U);
                if ((raw & 0x00800000) != 0) {
                    raw |= static_cast<std::int32_t>(0xFF000000);
                }
                sample = static_cast<float>(raw) / 8388608.0F;
            } else {
                sample = static_cast<float>(static_cast<std::int32_t>(ReadU32(bytes, offset))) / 2147483648.0F;
            }
            if (!std::isfinite(sample)) {
                return ImpulseResponseFileLoadError::NonFiniteSample;
            }
            sum += sample;
        }
        const auto mono = sum / static_cast<float>(channel_count);
        if (!std::isfinite(mono)) {
            return ImpulseResponseFileLoadError::NonFiniteSample;
        }
        imported.mono_samples.push_back(mono);
    }
    return imported;
}

ImpulseResponseConvolver::ImpulseResponseConvolver(std::uint32_t maximum_ir_length)
    : maximum_ir_length_(maximum_ir_length) {}

std::vector<ImpulseResponseLoadError> ImpulseResponseConvolver::LoadMonoImpulseResponse(std::span<const float> samples) {
    std::vector<ImpulseResponseLoadError> errors;
    if (samples.empty()) {
        errors.push_back(ImpulseResponseLoadError::EmptyImpulseResponse);
    }
    if (samples.size() > maximum_ir_length_) {
        errors.push_back(ImpulseResponseLoadError::TooLong);
    }
    for (const auto sample : samples) {
        if (!std::isfinite(sample)) {
            errors.push_back(ImpulseResponseLoadError::NonFiniteSample);
            break;
        }
    }
    if (!errors.empty()) {
        return errors;
    }

    impulse_response_.assign(samples.begin(), samples.end());
    history_.clear();
    history_position_ = 0;
    return {};
}

bool ImpulseResponseConvolver::Prepare(const AudioFormat& format) {
    if (!format.Validate().empty() || impulse_response_.empty()) {
        return false;
    }
    channel_count_ = format.channel_count;
    history_.assign(static_cast<std::size_t>(channel_count_) * impulse_response_.size(), 0.0F);
    history_position_ = 0;
    return true;
}

void ImpulseResponseConvolver::Process(const AudioBuffer& input,
                                       AudioBuffer& output,
                                       std::uint32_t sample_count) noexcept {
    const auto ir_size = static_cast<std::uint32_t>(impulse_response_.size());
    for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
        for (std::uint32_t channel = 0; channel < channel_count_; ++channel) {
            auto* channel_history = history_.data() + static_cast<std::size_t>(channel) * ir_size;
            channel_history[history_position_] = input.ChannelData(channel)[sample];

            float accumulated = 0.0F;
            for (std::uint32_t tap = 0; tap < ir_size; ++tap) {
                const auto history_index = (history_position_ + ir_size - tap) % ir_size;
                accumulated += channel_history[history_index] * impulse_response_[tap];
            }
            output.ChannelData(channel)[sample] = accumulated;
        }
        history_position_ = (history_position_ + 1U) % ir_size;
    }
}

std::uint32_t ImpulseResponseConvolver::DeclaredLatencySamples() const noexcept {
    return 0;
}

std::uint32_t ImpulseResponseConvolver::ImpulseResponseLength() const noexcept {
    return static_cast<std::uint32_t>(impulse_response_.size());
}

} // namespace solo_rig
