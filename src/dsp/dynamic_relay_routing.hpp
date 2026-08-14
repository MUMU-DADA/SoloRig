#pragma once

#include "dsp/dynamic_relay.hpp"

#include <cstdint>

namespace solo_rig {

struct DynamicRelayRoutingModel {
    std::uint32_t pole_count{1U};
    double contact_gap{};
};

enum class DynamicRelayRoutingError {
    None,
    InvalidModel,
    NonFinite,
};

struct DynamicRelayRoutingEvaluation {
    DynamicRelayRoutingError error{DynamicRelayRoutingError::None};
    double effective_position{};
    double normally_closed_resistance{};
    double normally_open_resistance{};

    [[nodiscard]] bool Succeeded() const noexcept {
        return error == DynamicRelayRoutingError::None;
    }
};

[[nodiscard]] bool ValidateDynamicRelayRoutingModel(
    const DynamicRelayRoutingModel& model) noexcept;
[[nodiscard]] DynamicRelayRoutingEvaluation EvaluateDynamicRelayRouting(
    const DynamicRelayModel& relay_model,
    const DynamicRelayRoutingModel& routing_model,
    const DynamicRelayState& state) noexcept;

} // namespace solo_rig
