#pragma once

#include "content/behavior_lowering.hpp"

#include <optional>
#include <string>
#include <vector>

namespace solo_rig {

inline constexpr std::size_t kMaximumBehaviorGraphNodes = 16U;
inline constexpr std::size_t kMaximumBehaviorGraphRoutes = 32U;

struct BehaviorGraphNodeSpec {
    std::string id;
    BehaviorBindingRequest binding;
};

struct BehaviorGraphRouteSpec {
    std::string source;
    std::string destination;
};

struct BehaviorGraphPlanNode {
    std::string id;
    BehaviorLoweringPlan lowering;
};

struct BehaviorGraphPlan {
    std::vector<BehaviorGraphPlanNode> nodes;
    std::vector<BehaviorGraphRouteSpec> routes;
    std::vector<std::string> topological_order;
};

enum class BehaviorGraphError {
    None,
    InvalidNode,
    DuplicateNode,
    InvalidRoute,
    DuplicateRoute,
    UnknownEndpoint,
    MultipleSignalInputs,
    MissingInputRoute,
    MissingOutputRoute,
    FanOutUnsupported,
    CycleDetected,
    CapacityExceeded,
    BlockedNode,
};

struct BehaviorGraphCompileResult {
    std::optional<BehaviorGraphPlan> plan;
    BehaviorGraphError error{BehaviorGraphError::None};
};

[[nodiscard]] BehaviorGraphCompileResult CompileBehaviorGraph(
    const std::vector<BehaviorGraphNodeSpec>& nodes,
    const std::vector<BehaviorGraphRouteSpec>& routes);

} // namespace solo_rig
