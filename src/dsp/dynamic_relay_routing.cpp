#include "dsp/dynamic_relay_routing.hpp"

#include <algorithm>
#include <cmath>

namespace solo_rig {

bool ValidateDynamicRelayRoutingModel(const DynamicRelayRoutingModel& model) noexcept {
    return model.pole_count >= 1U && model.pole_count <= 4U &&
           std::isfinite(model.contact_gap) && model.contact_gap >= 0.0 &&
           model.contact_gap <= 0.9;
}

DynamicRelayRoutingEvaluation EvaluateDynamicRelayRouting(
    const DynamicRelayModel& relay_model,
    const DynamicRelayRoutingModel& routing_model,
    const DynamicRelayState& state) noexcept {
    if (!ValidateDynamicRelayModel(relay_model) ||
        !ValidateDynamicRelayRoutingModel(routing_model)) {
        return {.error = DynamicRelayRoutingError::InvalidModel};
    }
    const auto effective = DynamicRelayEffectivePosition(relay_model, state);
    if (!effective.has_value()) {
        return {.error = DynamicRelayRoutingError::NonFinite};
    }

    const double lower = 0.5 - 0.5 * routing_model.contact_gap;
    const double upper = 0.5 + 0.5 * routing_model.contact_gap;
    const double normally_closed_fraction = *effective < lower
        ? std::clamp((lower - *effective) / lower, 0.0, 1.0)
        : 0.0;
    const double normally_open_fraction = *effective > upper
        ? std::clamp((*effective - upper) / (1.0 - upper), 0.0, 1.0)
        : 0.0;
    const auto resistance = [&relay_model](double closed_fraction) {
        return relay_model.open_resistance +
               closed_fraction * (relay_model.closed_resistance - relay_model.open_resistance);
    };
    const double normally_closed = resistance(normally_closed_fraction);
    const double normally_open = resistance(normally_open_fraction);
    if (!std::isfinite(normally_closed) || !std::isfinite(normally_open) ||
        normally_closed <= 0.0 || normally_open <= 0.0) {
        return {.error = DynamicRelayRoutingError::NonFinite};
    }
    return {.effective_position = *effective,
        .normally_closed_resistance = normally_closed,
        .normally_open_resistance = normally_open};
}

} // namespace solo_rig
