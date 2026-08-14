#include "dsp/nonlinear_transformer_core.hpp"

#include <algorithm>
#include <limits>

namespace solo_rig {

namespace {

double ClampFinite(double value, double minimum, double maximum) noexcept {
    return std::clamp(value, minimum, maximum);
}

} // namespace

bool ValidateNonlinearTransformerCoreModel(const NonlinearTransformerCoreModel& model) noexcept {
    return std::isfinite(model.magnetizing_inductance) && std::isfinite(model.saturation_flux) &&
           std::isfinite(model.hysteresis_width) &&
           std::isfinite(model.hysteresis_rate) && std::isfinite(model.remanent_flux) &&
           std::isfinite(model.max_flux) && std::isfinite(model.max_current) &&
           std::isfinite(model.max_step) && model.magnetizing_inductance >= 1.0e-12 &&
           model.magnetizing_inductance <= 1.0e6 && model.saturation_flux > 1.0e-12 &&
           model.saturation_flux <= 1.0e6 && model.hysteresis_width >= 0.0 &&
           model.hysteresis_width < model.max_flux && model.hysteresis_rate > 0.0 &&
           model.hysteresis_rate <= 1.0e9 && model.max_flux > model.saturation_flux &&
           model.max_flux <= 1.0e6 && std::fabs(model.remanent_flux) <= model.hysteresis_width &&
           model.max_current > 0.0 && model.max_current <= 1.0e6 && model.max_step > 0.0 &&
           model.max_step <= 1.0e6;
}

NonlinearTransformerCoreEvaluation EvaluateNonlinearTransformerCore(
    const NonlinearTransformerCoreModel& model, double magnetizing_current,
    double hysteresis) noexcept {
    if (!ValidateNonlinearTransformerCoreModel(model) || !std::isfinite(magnetizing_current) ||
        !std::isfinite(hysteresis)) {
        return {.flux = std::numeric_limits<double>::quiet_NaN(),
                .differential_flux = std::numeric_limits<double>::quiet_NaN(),
                .magnetizing_current = magnetizing_current};
    }
    const double current = ClampFinite(magnetizing_current, -model.max_current, model.max_current);
    const double bias = ClampFinite(hysteresis, -model.hysteresis_width, model.hysteresis_width);
    const double normalized = model.magnetizing_inductance * current / model.saturation_flux;
    const double shape = std::tanh(normalized);
    const double derivative = model.magnetizing_inductance * (1.0 - shape * shape);
    const double raw_flux = model.saturation_flux * shape + bias;
    if (!std::isfinite(raw_flux) || !std::isfinite(derivative)) {
        return {.flux = std::numeric_limits<double>::quiet_NaN(),
                .differential_flux = std::numeric_limits<double>::quiet_NaN(),
                .magnetizing_current = current};
    }
    if (std::fabs(raw_flux) >= model.max_flux) {
        const double sign = raw_flux < 0.0 ? -1.0 : 1.0;
        return {.flux = sign * model.max_flux, .differential_flux = 0.0,
                .magnetizing_current = current};
    }
    return {.flux = raw_flux, .differential_flux = derivative,
            .magnetizing_current = current};
}

bool AdvanceNonlinearTransformerCoreState(const NonlinearTransformerCoreModel& model,
                                          double magnetizing_current, double sample_period,
                                          NonlinearTransformerCoreState& state) noexcept {
    if (!ValidateNonlinearTransformerCoreModel(model) || !std::isfinite(sample_period) ||
        sample_period <= 0.0 || !std::isfinite(state.flux) || !std::isfinite(state.hysteresis) ||
        !std::isfinite(magnetizing_current)) {
        return false;
    }
    const double current = ClampFinite(magnetizing_current, -model.max_current, model.max_current);
    const double target = model.hysteresis_width * std::tanh(
        model.magnetizing_inductance * current / model.saturation_flux);
    const double alpha = 1.0 - std::exp(-model.hysteresis_rate * sample_period);
    state.hysteresis = ClampFinite(state.hysteresis + alpha * (target - state.hysteresis),
                                   -model.hysteresis_width, model.hysteresis_width);
    const auto evaluation = EvaluateNonlinearTransformerCore(model, current, state.hysteresis);
    if (!evaluation.Succeeded()) {
        return false;
    }
    state.flux = evaluation.flux;
    return true;
}

} // namespace solo_rig
