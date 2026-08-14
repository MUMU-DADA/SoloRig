#include "dsp/nonlinear_triode.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

bool ValidateNonlinearTriodeModel(const NonlinearTriodeModel& model) noexcept {
    return std::isfinite(model.amplification_factor) && model.amplification_factor >= 1.0 &&
           model.amplification_factor <= 1.0e6 && std::isfinite(model.perveance) &&
           model.perveance >= 1.0e-12 && model.perveance <= 1.0e6 &&
           std::isfinite(model.max_current) && model.max_current >= 1.0e-12 &&
           model.max_current <= 1.0e6 && std::isfinite(model.max_step) &&
           model.max_step >= 1.0e-20 && model.max_step <= 10.0;
}

NonlinearTriodeEvaluation EvaluateNonlinearTriode(
    const NonlinearTriodeModel& model, double plate, double grid, double cathode) noexcept {
    if (!ValidateNonlinearTriodeModel(model)) {
        return {.error = NonlinearDeviceError::InvalidModel};
    }
    if (!std::isfinite(plate) || !std::isfinite(grid) || !std::isfinite(cathode)) {
        return {.error = NonlinearDeviceError::NonFinite};
    }
    const double scale = std::max({1.0, std::fabs(plate), std::fabs(grid), std::fabs(cathode)});
    const double normalized_plate_cathode = plate / scale - cathode / scale;
    const double normalized_control = grid / scale - cathode / scale +
        normalized_plate_cathode / model.amplification_factor;
    if (normalized_plate_cathode <= 0.0 || normalized_control <= 0.0) {
        return {.error = NonlinearDeviceError::None};
    }
    const double control_limit = std::pow(model.max_current / model.perveance, 2.0 / 3.0);
    if (normalized_control >= control_limit / scale) {
        NonlinearTriodeEvaluation clamped{.error = NonlinearDeviceError::None};
        clamped.currents[0] = model.max_current;
        clamped.currents[2] = -model.max_current;
        return clamped;
    }
    const double control = normalized_control * scale;
    const double root = std::sqrt(control);
    const double current = model.perveance * control * root;
    const double derivative_control = 1.5 * model.perveance * root;
    if (!std::isfinite(current) || !std::isfinite(derivative_control)) {
        return {.error = NonlinearDeviceError::NonFinite};
    }
    const double derivatives[3] = {
        derivative_control / model.amplification_factor,
        derivative_control,
        -derivative_control * (1.0 + 1.0 / model.amplification_factor)};
    NonlinearTriodeEvaluation result{.error = NonlinearDeviceError::None};
    result.currents[0] = current;
    result.currents[2] = -current;
    for (std::size_t column = 0; column < 3U; ++column) {
        result.jacobian[0][column] = derivatives[column];
        result.jacobian[2][column] = -derivatives[column];
    }
    return result;
}

} // namespace solo_rig
