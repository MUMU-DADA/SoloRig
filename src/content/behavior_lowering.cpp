#include "content/behavior_lowering.hpp"

#include <string_view>

namespace solo_rig {

namespace {

std::optional<BehaviorKind> ParseKind(std::string_view value) {
    if (value == "bbd") return BehaviorKind::Bbd;
    if (value == "vco") return BehaviorKind::Vco;
    if (value == "vca") return BehaviorKind::Vca;
    if (value == "spring") return BehaviorKind::Spring;
    return std::nullopt;
}

} // namespace

BehaviorLoweringResult LowerBehaviorModule(const BehaviorBindingRequest& request) {
    if (request.module_id.empty()) {
        return {.plan = std::nullopt, .error = BehaviorLoweringError::InvalidBinding};
    }
    const auto kind = ParseKind(request.kind);
    if (!kind.has_value()) {
        return {.plan = std::nullopt, .error = BehaviorLoweringError::UnknownBehavior};
    }
    // M15 deliberately has no authorized behavior model or runtime lowering.
    // Keep the binding visible as a refusal instead of hard-coding a pedal.
    return {.plan = std::nullopt, .error = BehaviorLoweringError::BlockedModel};
}

std::vector<BehaviorKindContract> BehaviorKindContracts() {
    return {
        {BehaviorKind::Bbd, WhiteboxPlanStatus::Blocked},
        {BehaviorKind::Vco, WhiteboxPlanStatus::Blocked},
        {BehaviorKind::Vca, WhiteboxPlanStatus::Blocked},
        {BehaviorKind::Spring, WhiteboxPlanStatus::Blocked},
    };
}

} // namespace solo_rig
