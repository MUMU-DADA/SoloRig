#pragma once

#include "standalone/asio_driver_abi.hpp"

#include <cstdint>

namespace solo_rig::standalone {

[[nodiscard]] bool IsSupportedAsioSampleType(asio_abi::ASIOSampleType sample_type) noexcept;

[[nodiscard]] bool ConvertAsioInputToFloat32(const void* source,
                                             float* destination,
                                             asio_abi::ASIOSampleType sample_type,
                                             std::uint32_t sample_count) noexcept;

[[nodiscard]] bool ConvertFloat32ToAsioOutput(const float* source,
                                              void* destination,
                                              asio_abi::ASIOSampleType sample_type,
                                              std::uint32_t sample_count) noexcept;

} // namespace solo_rig::standalone
