#include "core/quality_policy.hpp"

#include <array>

namespace solo_rig {

OversamplingPlan CalculateOversamplingPlan(double host_sample_rate_hz,
                                           QualityMode quality,
                                           bool has_nonlinear_processing) {
    if (!has_nonlinear_processing || quality == QualityMode::Draft) {
        return {.factor = 1, .processing_sample_rate_hz = host_sample_rate_hz};
    }

    const std::uint32_t requested_factor = [&quality] {
        switch (quality) {
        case QualityMode::Standard:
            return 2U;
        case QualityMode::High:
            return 4U;
        case QualityMode::Maximum:
            return 8U;
        case QualityMode::Draft:
            return 1U;
        }
        return 1U;
    }();

    constexpr double maximum_oversampled_rate_hz = 192000.0;
    constexpr std::array<std::uint32_t, 4> supported_factors{8U, 4U, 2U, 1U};
    for (const auto factor : supported_factors) {
        if (factor <= requested_factor && host_sample_rate_hz * static_cast<double>(factor) <=
                                            maximum_oversampled_rate_hz) {
            return {.factor = factor,
                    .processing_sample_rate_hz = host_sample_rate_hz * static_cast<double>(factor)};
        }
    }

    return {.factor = 1, .processing_sample_rate_hz = host_sample_rate_hz};
}

} // namespace solo_rig
