#include "dsp/nonlinear_pentode.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

bool ValidateNonlinearPentodeModel(const NonlinearPentodeModel& model) noexcept {
    return std::isfinite(model.screen_amplification_factor) &&
           model.screen_amplification_factor >= 1.0 && model.screen_amplification_factor <= 1.0e6 &&
           std::isfinite(model.perveance) && model.perveance >= 1.0e-12 && model.perveance <= 1.0e6 &&
           std::isfinite(model.knee_voltage) && model.knee_voltage >= 1.0e-6 &&
           model.knee_voltage <= 1.0e6 && std::isfinite(model.screen_current_fraction) &&
           model.screen_current_fraction >= 0.0 && model.screen_current_fraction <= 0.5 &&
           std::isfinite(model.max_current) && model.max_current >= 1.0e-12 &&
           model.max_current <= 1.0e6 && std::isfinite(model.max_step) &&
           model.max_step >= 1.0e-20 && model.max_step <= 10.0;
}

NonlinearPentodeEvaluation EvaluateNonlinearPentode(
    const NonlinearPentodeModel& model, double plate, double screen, double grid, double cathode) noexcept {
    if (!ValidateNonlinearPentodeModel(model)) {
        return {.error = NonlinearDeviceError::InvalidModel};
    }
    if (!std::isfinite(plate) || !std::isfinite(screen) ||
        !std::isfinite(grid) || !std::isfinite(cathode)) {
        return {.error = NonlinearDeviceError::NonFinite};
    }
    const double scale = std::max({1.0, std::fabs(plate), std::fabs(screen),
                                   std::fabs(grid), std::fabs(cathode)});
    const double normalized_vpk = plate / scale - cathode / scale;
    const double normalized_vsk = screen / scale - cathode / scale;
    const double normalized_control = grid / scale - cathode / scale +
        normalized_vsk / model.screen_amplification_factor;
    if (normalized_vpk <= 0.0 || normalized_vsk <= 0.0 || normalized_control <= 0.0) {
        return {.error = NonlinearDeviceError::None};
    }

    const double control_limit = std::pow(model.max_current / model.perveance, 2.0 / 3.0);
    double space_current = model.max_current;
    double derivative_control = 0.0;
    if (normalized_control < control_limit / scale) {
        const double control = normalized_control * scale;
        const double root = std::sqrt(control);
        space_current = model.perveance * control * root;
        derivative_control = 1.5 * model.perveance * root;
    }

    double plate_share = 1.0 - std::exp(-60.0);
    double derivative_share = 0.0;
    if (normalized_vpk < 60.0 * model.knee_voltage / scale) {
        const double exponent = std::exp(-(normalized_vpk * scale) / model.knee_voltage);
        plate_share = 1.0 - exponent;
        derivative_share = exponent / model.knee_voltage;
    }
    const double plate_fraction = 1.0 - model.screen_current_fraction;
    const double plate_current = plate_fraction * space_current * plate_share;
    const double screen_current = model.screen_current_fraction * space_current;
    if (!std::isfinite(plate_current) || !std::isfinite(screen_current)) {
        return {.error = NonlinearDeviceError::NonFinite};
    }

    const double control_derivatives[4] = {
        0.0,
        1.0 / model.screen_amplification_factor,
        1.0,
        -(1.0 + 1.0 / model.screen_amplification_factor)};
    const double share_derivatives[4] = {
        derivative_share, 0.0, 0.0, -derivative_share};
    NonlinearPentodeEvaluation result{.error = NonlinearDeviceError::None};
    result.currents[0] = plate_current;
    result.currents[1] = screen_current;
    result.currents[3] = -(plate_current + screen_current);
    for (std::size_t column = 0; column < 4U; ++column) {
        const double d_space = derivative_control * control_derivatives[column];
        const double d_plate = plate_fraction *
            (d_space * plate_share + space_current * share_derivatives[column]);
        const double d_screen = model.screen_current_fraction * d_space;
        result.jacobian[0][column] = d_plate;
        result.jacobian[1][column] = d_screen;
        result.jacobian[3][column] = -(d_plate + d_screen);
    }
    return result;
}

} // namespace solo_rig
