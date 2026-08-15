#pragma once

#include "content/behavior_ports.hpp"

namespace solo_rig {

enum class BehaviorClockedCompileError {
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
    CapacityExceeded,
    UnsupportedBlock,
};

struct BehaviorClockedGraphPlan {
    std::vector<BehaviorGraphPlanNode> nodes;
    std::vector<BehaviorPortRouteSpec> routes;
    std::vector<std::string> topological_order;
};

struct BehaviorClockedCompileResult {
    std::optional<BehaviorClockedGraphPlan> plan;
    BehaviorClockedCompileError error{BehaviorClockedCompileError::None};
};

// Compile-only contract for the explicit input.audio -> BBD.audio_in,
// VCO.out(control) -> BBD.clock, BBD.out(audio) -> output.audio chain.
[[nodiscard]] BehaviorClockedCompileResult CompileVcoBbdClockedGraph(
    const std::vector<BehaviorGraphNodeSpec>& nodes,
    const std::vector<BehaviorPortRouteSpec>& routes);

} // namespace solo_rig
