#pragma once

#include <cmath>

namespace solo_rig {

// Self-authored generic core law. Flux and hysteresis are normalized state
// variables; they are not commercial material parameters.
struct NonlinearTransformerCoreModel {
    double magnetizing_inductance{1.0};
    double saturation_flux{1.0};
    double hysteresis_width{0.1};
    double hysteresis_rate{1000.0};
    double remanent_flux{};
    double max_flux{2.0};
    double max_current{10.0};
    double max_step{1.0};
};

struct NonlinearTransformerCoreState {
    double flux{};
    double hysteresis{};
};

struct NonlinearTransformerCoreEvaluation {
    double flux{};
    double differential_flux{};
    double magnetizing_current{};

    [[nodiscard]] bool Succeeded() const noexcept {
        return std::isfinite(flux) && std::isfinite(differential_flux) &&
               std::isfinite(magnetizing_current) && differential_flux >= 0.0;
    }
};

[[nodiscard]] bool ValidateNonlinearTransformerCoreModel(
    const NonlinearTransformerCoreModel& model) noexcept;
[[nodiscard]] NonlinearTransformerCoreEvaluation EvaluateNonlinearTransformerCore(
    const NonlinearTransformerCoreModel& model, double magnetizing_current,
    double hysteresis) noexcept;
[[nodiscard]] bool AdvanceNonlinearTransformerCoreState(
    const NonlinearTransformerCoreModel& model, double magnetizing_current,
    double sample_period, NonlinearTransformerCoreState& state) noexcept;

} // namespace solo_rig
