#pragma once

#include "dsp/generic_optocoupler.hpp"

namespace solo_rig {

struct GenericOptocouplerMnaModel final {
    GenericOptocouplerModel optical{};
    double emitter_resistance_ohms{100.0};
};

struct GenericOptocouplerMnaEvaluation final {
    double next_illumination{};
    double ldr_resistance_ohms{};
};

[[nodiscard]] bool ValidateGenericOptocouplerMnaModel(
    const GenericOptocouplerMnaModel& model) noexcept;
[[nodiscard]] GenericOptocouplerError EvaluateGenericOptocouplerMna(
    const GenericOptocouplerMnaModel& model, double emitter_drive_volts,
    double previous_illumination, double timestep_seconds,
    GenericOptocouplerMnaEvaluation& output) noexcept;

} // namespace solo_rig
