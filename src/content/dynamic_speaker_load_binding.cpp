#include "content/dynamic_speaker_load_binding.hpp"

#include "dsp/dynamic_speaker_load.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace solo_rig {
namespace {

const double* Parameter(const ModelAuditRecord& record, std::string_view id) noexcept {
    const auto it = std::find_if(record.parameters.begin(), record.parameters.end(),
                                 [id](const auto& parameter) { return parameter.id == id; });
    return it == record.parameters.end() ? nullptr : &it->default_value;
}

} // namespace

bool BindAuditedSpeakerLoadModel(const ModelAuditRecord& record,
                                 CircuitSpeakerLoadModel& destination) noexcept {
    if (record.kind != ModelKind::SpeakerLoad ||
        record.part_number != "self-authored-electrodynamic-speaker-load-no-commercial-part") {
        return false;
    }
    const auto value = [&record](std::string_view id) { return Parameter(record, id); };
    const double* re = value("voice-coil-resistance");
    const double* le = value("voice-coil-inductance");
    const double* bl = value("force-factor");
    const double* mass = value("moving-mass");
    const double* damping = value("mechanical-resistance");
    const double* compliance = value("compliance");
    const double* nonlinear = value("nonlinear-stiffness");
    const double* scale = value("nonlinear-displacement-scale");
    const double* maximum_current = value("maximum-current");
    const double* maximum_velocity = value("maximum-velocity");
    const double* maximum_displacement = value("maximum-displacement");
    const double* initial_current = value("initial-current");
    const double* initial_velocity = value("initial-velocity");
    const double* initial_displacement = value("initial-displacement");
    if (!re || !le || !bl || !mass || !damping || !compliance || !nonlinear || !scale ||
        !maximum_current || !maximum_velocity || !maximum_displacement || !initial_current ||
        !initial_velocity || !initial_displacement) return false;
    const DynamicSpeakerLoadModel model{*re, *le, *bl, *mass, *damping, *compliance,
        *nonlinear, *scale, *maximum_current, *maximum_velocity, *maximum_displacement};
    if (!ValidateDynamicSpeakerLoadModel(model) || std::fabs(*initial_current) > *maximum_current ||
        std::fabs(*initial_velocity) > *maximum_velocity ||
        std::fabs(*initial_displacement) > *maximum_displacement) return false;
    destination = {.kind = "electrodynamic", .model_version = 1,
        .voice_coil_resistance = *re, .voice_coil_inductance = *le,
        .force_factor = *bl, .moving_mass = *mass, .mechanical_resistance = *damping,
        .compliance = *compliance, .nonlinear_stiffness = *nonlinear,
        .nonlinear_displacement_scale = *scale, .maximum_current = *maximum_current,
        .maximum_velocity = *maximum_velocity, .maximum_displacement = *maximum_displacement,
        .initial_current = *initial_current, .initial_velocity = *initial_velocity,
        .initial_displacement = *initial_displacement};
    return true;
}

} // namespace solo_rig
