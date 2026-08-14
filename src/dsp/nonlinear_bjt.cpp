#include "dsp/nonlinear_bjt.hpp"

#include "dsp/nonlinear_exponential.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

bool ValidateNonlinearBjtModel(const NonlinearBjtModel& model) noexcept {
    return (model.polarity == BjtPolarity::Npn || model.polarity == BjtPolarity::Pnp) &&
           std::isfinite(model.saturation_current) && model.saturation_current >= 1.0e-30 &&
           model.saturation_current <= 1.0e-3 && std::isfinite(model.forward_beta) &&
           model.forward_beta >= 0.01 && model.forward_beta <= 1.0e6 &&
           std::isfinite(model.reverse_beta) && model.reverse_beta >= 0.01 &&
           model.reverse_beta <= 1.0e6 && std::isfinite(model.thermal_voltage) &&
           model.thermal_voltage >= 1.0e-6 && model.thermal_voltage <= 1.0 &&
           std::isfinite(model.max_current) && model.max_current >= 1.0e-12 &&
           model.max_current <= 1.0e6 && std::isfinite(model.max_step) &&
           model.max_step >= 1.0e-20 && model.max_step <= 10.0;
}

NonlinearBjtEvaluation EvaluateNonlinearBjt(
    const NonlinearBjtModel& model, double collector, double base, double emitter) noexcept {
    if (!ValidateNonlinearBjtModel(model)) {
        return {.error = NonlinearDeviceError::InvalidModel};
    }
    if (!std::isfinite(collector) || !std::isfinite(base) || !std::isfinite(emitter)) {
        return {.error = NonlinearDeviceError::NonFinite};
    }
    const auto polarity = model.polarity == BjtPolarity::Npn ? 1.0 : -1.0;
    const auto forward = nonlinear_detail::BoundedExponential(
        polarity * (base - emitter) / model.thermal_voltage);
    const auto reverse = nonlinear_detail::BoundedExponential(
        polarity * (base - collector) / model.thermal_voltage);
    auto forward_current = model.saturation_current * (forward.value - 1.0);
    auto reverse_current = model.saturation_current * (reverse.value - 1.0);
    auto forward_conductance = model.saturation_current / model.thermal_voltage * forward.derivative_scale;
    auto reverse_conductance = model.saturation_current / model.thermal_voltage * reverse.derivative_scale;
    if (std::fabs(forward_current) >= model.max_current) {
        forward_current = std::clamp(forward_current, -model.max_current, model.max_current);
        forward_conductance = 0.0;
    }
    if (std::fabs(reverse_current) >= model.max_current) {
        reverse_current = std::clamp(reverse_current, -model.max_current, model.max_current);
        reverse_conductance = 0.0;
    }
    const auto forward_alpha = model.forward_beta / (model.forward_beta + 1.0);
    const auto reverse_alpha = model.reverse_beta / (model.reverse_beta + 1.0);
    const double currents[3] = {
        polarity * (forward_alpha * forward_current - reverse_current),
        polarity * ((1.0 - forward_alpha) * forward_current +
                    (1.0 - reverse_alpha) * reverse_current),
        polarity * (-forward_current + reverse_alpha * reverse_current)};
    const double jacobian[3][3] = {
        {reverse_conductance,
         forward_alpha * forward_conductance - reverse_conductance,
         -forward_alpha * forward_conductance},
        {-(1.0 - reverse_alpha) * reverse_conductance,
         (1.0 - forward_alpha) * forward_conductance +
             (1.0 - reverse_alpha) * reverse_conductance,
         -(1.0 - forward_alpha) * forward_conductance},
        {-reverse_alpha * reverse_conductance,
         -forward_conductance + reverse_alpha * reverse_conductance,
         forward_conductance}};
    NonlinearBjtEvaluation result{.error = NonlinearDeviceError::None};
    for (std::size_t row = 0; row < 3U; ++row) {
        result.currents[row] = currents[row];
        for (std::size_t column = 0; column < 3U; ++column) {
            result.jacobian[row][column] = jacobian[row][column];
        }
    }
    for (const auto current : result.currents) {
        if (!std::isfinite(current)) {
            return {.error = NonlinearDeviceError::NonFinite};
        }
    }
    return result;
}

} // namespace solo_rig
