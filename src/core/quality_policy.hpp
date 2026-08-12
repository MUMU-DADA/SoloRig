#pragma once

#include <cstdint>

namespace solo_rig {

enum class QualityMode {
    Draft,
    Standard,
    High,
    Maximum,
};

struct OversamplingPlan {
    std::uint32_t factor{};
    double processing_sample_rate_hz{};
};

[[nodiscard]] OversamplingPlan CalculateOversamplingPlan(double host_sample_rate_hz,
                                                          QualityMode quality,
                                                          bool has_nonlinear_processing);

} // namespace solo_rig
