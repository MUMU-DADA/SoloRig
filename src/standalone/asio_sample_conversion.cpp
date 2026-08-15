#include "standalone/asio_sample_conversion.hpp"

#include <algorithm>

namespace solo_rig::standalone {

bool IsSupportedAsioSampleType(asio_abi::ASIOSampleType sample_type) noexcept {
    return sample_type == asio_abi::kInt16Lsb || sample_type == asio_abi::kInt24Lsb ||
           sample_type == asio_abi::kInt32Lsb;
}

bool ConvertAsioInputToFloat32(const void* source,
                               float* destination,
                               asio_abi::ASIOSampleType sample_type,
                               std::uint32_t sample_count) noexcept {
    if (sample_count == 0U) {
        return true;
    }
    if (source == nullptr || destination == nullptr || !IsSupportedAsioSampleType(sample_type)) {
        return false;
    }
    if (sample_type == asio_abi::kInt16Lsb) {
        const auto* samples = static_cast<const std::int16_t*>(source);
        for (std::uint32_t index = 0; index < sample_count; ++index) {
            destination[index] = static_cast<float>(samples[index]) / 32768.0F;
        }
        return true;
    }
    if (sample_type == asio_abi::kInt24Lsb) {
        const auto* bytes = static_cast<const std::uint8_t*>(source);
        for (std::uint32_t index = 0; index < sample_count; ++index) {
            const auto offset = static_cast<std::size_t>(index) * 3U;
            auto sample = static_cast<std::int32_t>(bytes[offset]) |
                          (static_cast<std::int32_t>(bytes[offset + 1U]) << 8) |
                          (static_cast<std::int32_t>(bytes[offset + 2U]) << 16);
            if ((sample & 0x00800000) != 0) {
                sample |= static_cast<std::int32_t>(0xFF000000U);
            }
            destination[index] = static_cast<float>(sample) / 8388608.0F;
        }
        return true;
    }
    const auto* samples = static_cast<const std::int32_t*>(source);
    for (std::uint32_t index = 0; index < sample_count; ++index) {
        destination[index] = static_cast<float>(samples[index]) / 2147483648.0F;
    }
    return true;
}

bool ConvertFloat32ToAsioOutput(const float* source,
                                void* destination,
                                asio_abi::ASIOSampleType sample_type,
                                std::uint32_t sample_count) noexcept {
    if (sample_count == 0U) {
        return true;
    }
    if (source == nullptr || destination == nullptr || !IsSupportedAsioSampleType(sample_type)) {
        return false;
    }
    if (sample_type == asio_abi::kInt16Lsb) {
        auto* samples = static_cast<std::int16_t*>(destination);
        for (std::uint32_t index = 0; index < sample_count; ++index) {
            samples[index] = static_cast<std::int16_t>(std::clamp(source[index], -1.0F, 1.0F) * 32767.0F);
        }
        return true;
    }
    if (sample_type == asio_abi::kInt24Lsb) {
        auto* bytes = static_cast<std::uint8_t*>(destination);
        for (std::uint32_t index = 0; index < sample_count; ++index) {
            const auto sample = static_cast<std::int32_t>(std::clamp(source[index], -1.0F, 1.0F) * 8388607.0F);
            const auto offset = static_cast<std::size_t>(index) * 3U;
            bytes[offset] = static_cast<std::uint8_t>(sample & 0xFF);
            bytes[offset + 1U] = static_cast<std::uint8_t>((sample >> 8) & 0xFF);
            bytes[offset + 2U] = static_cast<std::uint8_t>((sample >> 16) & 0xFF);
        }
        return true;
    }
    auto* samples = static_cast<std::int32_t*>(destination);
    for (std::uint32_t index = 0; index < sample_count; ++index) {
        samples[index] = static_cast<std::int32_t>(std::clamp(source[index], -1.0F, 1.0F) * 2147483647.0F);
    }
    return true;
}

} // namespace solo_rig::standalone
