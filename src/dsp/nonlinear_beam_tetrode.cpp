#include "dsp/nonlinear_beam_tetrode.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

bool ValidateNonlinearBeamTetrodeModel(const NonlinearBeamTetrodeModel& model) noexcept {
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

NonlinearBeamTetrodeEvaluation EvaluateNonlinearBeamTetrode(
    const NonlinearBeamTetrodeModel& model, double plate, double screen, double grid, double cathode) noexcept {
    if (!ValidateNonlinearBeamTetrodeModel(model)) {
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

    const double normalized_knee = model.knee_voltage / scale;
    double capture{};
    double derivative_capture{};
    if (normalized_vpk <= normalized_knee) {
        const double x = normalized_vpk / normalized_knee;
        const double denominator = 1.0 + x * x;
        capture = x * x / denominator;
        derivative_capture = 2.0 * x /
            (model.knee_voltage * denominator * denominator);
    } else {
        const double inverse = normalized_knee / normalized_vpk;
        const double denominator = 1.0 + inverse * inverse;
        capture = 1.0 / denominator;
        derivative_capture = 2.0 * inverse * inverse * inverse /
            (model.knee_voltage * denominator * denominator);
    }

    const double plate_current = space_current * capture;
    const double screen_current = model.screen_current_fraction *
        space_current * (1.0 - capture);
    if (!std::isfinite(plate_current) || !std::isfinite(screen_current) ||
        !std::isfinite(derivative_capture)) {
        return {.error = NonlinearDeviceError::NonFinite};
    }

    const double control_derivatives[4] = {
        0.0,
        1.0 / model.screen_amplification_factor,
        1.0,
        -(1.0 + 1.0 / model.screen_amplification_factor)};
    const double capture_derivatives[4] = {
        derivative_capture, 0.0, 0.0, -derivative_capture};

    NonlinearBeamTetrodeEvaluation result{.error = NonlinearDeviceError::None};
    result.currents[0] = plate_current;
    result.currents[1] = screen_current;
    result.currents[3] = -(plate_current + screen_current);
    for (std::size_t column = 0; column < 4U; ++column) {
        const double d_space = derivative_control * control_derivatives[column];
        const double d_capture = capture_derivatives[column];
        const double d_plate = d_space * capture + space_current * d_capture;
        const double d_screen = model.screen_current_fraction *
            (d_space * (1.0 - capture) - space_current * d_capture);
        result.jacobian[0][column] = d_plate;
        result.jacobian[1][column] = d_screen;
        result.jacobian[3][column] = -(d_plate + d_screen);
    }
    return result;
}

} // namespace solo_rig
