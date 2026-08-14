#pragma once

#include <algorithm>
#include <cmath>

namespace solo_rig::nonlinear_detail {

struct ExponentialTerm {
    double value{};
    double derivative_scale{};
};

[[nodiscard]] inline ExponentialTerm BoundedExponential(double argument) noexcept {
    constexpr double maximum_exponent = 60.0;
    const auto limited = std::clamp(argument, -maximum_exponent, maximum_exponent);
    const auto value = std::exp(limited);
    return {.value = value,
            .derivative_scale = argument == limited ? value : 0.0};
}

} // namespace solo_rig::nonlinear_detail
