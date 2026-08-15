#pragma once

#include "content/behavior_ports.hpp"

namespace solo_rig {

enum class BehaviorFeedbackCompileError {
    None,
    InvalidPlan,
    DuplicateNode,
    InvalidRoute,
    DuplicateRoute,
    UnknownEndpoint,
    UnknownPort,
    DirectionMismatch,
    MissingPort,
    FanInUnsupported,
    FanOutUnsupported,
    IncompatibleConnection,
    CycleDetected,
    ZeroDelayCycle,
    MultipleFeedback,
    CapacityExceeded,
    UnsupportedBlock,
};

struct BehaviorFeedbackGraphPlan {
    std::vector<BehaviorGraphPlanNode> nodes;
    std::vector<BehaviorPortRouteSpec> routes;
    std::vector<std::string> topological_order;
};

struct BehaviorFeedbackCompileResult {
    std::optional<BehaviorFeedbackGraphPlan> plan;
    BehaviorFeedbackCompileError error{BehaviorFeedbackCompileError::None};
};

// Compile-only contract for the single explicit delayed feedback edge.
[[nodiscard]] BehaviorFeedbackCompileResult CompileVcoBbdFeedbackGraph(
    const std::vector<BehaviorGraphNodeSpec>& nodes,
    const std::vector<BehaviorPortRouteSpec>& routes);

} // namespace solo_rig
