#include "dsp/nonlinear_vari_mu.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

bool ValidateNonlinearVariMuModel(const NonlinearVariMuModel& model) noexcept {
    return std::isfinite(model.amplification_factor) && model.amplification_factor >= 1.0 &&
           model.amplification_factor <= 1.0e6 && std::isfinite(model.perveance) &&
           model.perveance >= 1.0e-12 && model.perveance <= 1.0e6 &&
           std::isfinite(model.remote_cutoff_coefficient) && model.remote_cutoff_coefficient >= 1.0e-12 &&
           model.remote_cutoff_coefficient <= 1.0e6 && std::isfinite(model.max_current) &&
           model.max_current >= 1.0e-12 && model.max_current <= 1.0e6 &&
           std::isfinite(model.max_step) && model.max_step >= 1.0e-20 && model.max_step <= 10.0;
}

NonlinearVariMuEvaluation EvaluateNonlinearVariMu(
    const NonlinearVariMuModel& model, double plate, double grid, double cathode) noexcept {
    if (!ValidateNonlinearVariMuModel(model)) {
        return {.error = NonlinearDeviceError::InvalidModel};
    }
    if (!std::isfinite(plate) || !std::isfinite(grid) || !std::isfinite(cathode)) {
        return {.error = NonlinearDeviceError::NonFinite};
    }

    const double voltage_scale = std::max({1.0, std::fabs(plate), std::fabs(grid), std::fabs(cathode)});
    const double normalized_vpk = plate / voltage_scale - cathode / voltage_scale;
    const double normalized_vgk = grid / voltage_scale - cathode / voltage_scale;
    const double normalized_control = normalized_vgk +
        normalized_vpk / model.amplification_factor;
    if (normalized_vpk <= 0.0 || normalized_control <= 0.0) {
        return {.error = NonlinearDeviceError::None};
    }

    double remote_scale = 1.0;
    double derivative_log_remote_vgk = 0.0;
    if (normalized_vgk < 0.0) {
        const double normalized_bias = -normalized_vgk;
        const double normalized_inverse_coefficient =
            (1.0 / model.remote_cutoff_coefficient) / voltage_scale;
        if (normalized_bias <= normalized_inverse_coefficient) {
            const double q = normalized_bias / normalized_inverse_coefficient;
            const double denominator = 1.0 + q * q;
            remote_scale = 1.0 / std::sqrt(denominator);
            derivative_log_remote_vgk = model.remote_cutoff_coefficient * q / denominator;
        } else {
            const double inverse = normalized_inverse_coefficient / normalized_bias;
            const double denominator = 1.0 + inverse * inverse;
            remote_scale = inverse / std::sqrt(denominator);
            derivative_log_remote_vgk =
                model.remote_cutoff_coefficient * inverse / denominator;
        }
    }
    if (!std::isfinite(remote_scale) || !std::isfinite(derivative_log_remote_vgk)) {
        return {.error = NonlinearDeviceError::NonFinite};
    }
    if (remote_scale == 0.0) {
        return {.error = NonlinearDeviceError::None};
    }

    const double log_control = std::log(normalized_control) + std::log(voltage_scale);
    const double log_current = std::log(model.perveance) + std::log(remote_scale) + 1.5 * log_control;
    if (log_current >= std::log(model.max_current)) {
        NonlinearVariMuEvaluation clamped{.error = NonlinearDeviceError::None};
        clamped.currents[0] = model.max_current;
        clamped.currents[2] = -model.max_current;
        return clamped;
    }

    const double current = std::exp(log_current);
    const double derivative_control =
        1.5 * (current / voltage_scale) / normalized_control;
    const double derivative_remote_vgk = current * derivative_log_remote_vgk;
    if (!std::isfinite(current) || !std::isfinite(derivative_control) ||
        !std::isfinite(derivative_remote_vgk)) {
        return {.error = NonlinearDeviceError::NonFinite};
    }

    const double inverse_amplification = 1.0 / model.amplification_factor;
    const double derivatives[3] = {
        derivative_control * inverse_amplification,
        derivative_control + derivative_remote_vgk,
        -derivative_control * (1.0 + inverse_amplification) - derivative_remote_vgk};
    NonlinearVariMuEvaluation result{.error = NonlinearDeviceError::None};
    result.currents[0] = current;
    result.currents[2] = -current;
    for (std::size_t column = 0; column < 3U; ++column) {
        result.jacobian[0][column] = derivatives[column];
        result.jacobian[2][column] = -derivatives[column];
    }
    return result;
}

} // namespace solo_rig
