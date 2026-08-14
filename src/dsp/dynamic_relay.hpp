#pragma once

#include <optional>

namespace solo_rig {

struct DynamicRelayModel {
    double coil_resistance{};
    double coil_inductance{};
    double open_resistance{};
    double closed_resistance{};
    double pull_in_current{};
    double drop_out_current{};
    double operate_time_seconds{};
    double release_time_seconds{};
    double bounce_duration_seconds{};
    double bounce_frequency_hz{};
    double bounce_amplitude{};
    double maximum_coil_current{};
};

struct DynamicRelayState {
    double coil_current{};
    double position{};
    double bounce_remaining_seconds{};
    double bounce_phase{};
    bool energized{};
};

enum class DynamicRelayError {
    None,
    InvalidModel,
    NonFinite,
};

struct DynamicRelayEvaluation {
    DynamicRelayError error{DynamicRelayError::None};
    double coil_voltage{};
    double coil_voltage_derivative_current{};
    double contact_resistance{};
    DynamicRelayState next_state{};

    [[nodiscard]] bool Succeeded() const noexcept { return error == DynamicRelayError::None; }
};

[[nodiscard]] bool ValidateDynamicRelayModel(const DynamicRelayModel& model) noexcept;
[[nodiscard]] std::optional<double> DynamicRelayEffectivePosition(
    const DynamicRelayModel& model,
    const DynamicRelayState& state) noexcept;
[[nodiscard]] DynamicRelayEvaluation EvaluateDynamicRelay(
    const DynamicRelayModel& model,
    double coil_current,
    const DynamicRelayState& previous_state,
    double timestep_seconds) noexcept;

} // namespace solo_rig
