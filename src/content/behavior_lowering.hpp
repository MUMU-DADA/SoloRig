#pragma once

#include "content/topology_decomposition.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace solo_rig {

enum class BehaviorKind {
    Bbd,
    Vco,
    Vca,
    Spring,
};

enum class BehaviorLoweringError {
    InvalidBinding,
    UnknownBehavior,
    BlockedModel,
    UnsupportedModel,
};

struct BehaviorBindingRequest {
    std::string module_id;
    std::string kind;
    std::string model_id;
};

struct BehaviorLoweringPlan {
    std::string module_id;
    BehaviorKind kind{BehaviorKind::Bbd};
    std::string model_id;
    WhiteboxPlanStatus status{WhiteboxPlanStatus::Blocked};
};

struct BehaviorLoweringResult {
    std::optional<BehaviorLoweringPlan> plan;
    std::optional<BehaviorLoweringError> error;
};

struct BehaviorKindContract {
    BehaviorKind kind{BehaviorKind::Bbd};
    WhiteboxPlanStatus status{WhiteboxPlanStatus::Blocked};
};

[[nodiscard]] BehaviorLoweringResult LowerBehaviorModule(const BehaviorBindingRequest& request);
[[nodiscard]] std::vector<BehaviorKindContract> BehaviorKindContracts();

} // namespace solo_rig
