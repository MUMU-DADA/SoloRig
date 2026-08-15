#pragma once

#include "content/behavior_graph.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace solo_rig {

enum class BehaviorPortDirection {
    Input,
    Output,
};

enum class BehaviorPortDomain {
    AudioSignal,
    ControlSignal,
    DetectorInput,
    EmitterDrive,
    Resistance,
};

struct BehaviorPortRouteSpec {
    std::string source;
    std::string source_port;
    std::string destination;
    std::string destination_port;
};

struct BehaviorTypedGraphPlan {
    std::vector<BehaviorGraphPlanNode> nodes;
    std::vector<BehaviorPortRouteSpec> routes;
    std::vector<std::string> topological_order;
};

enum class BehaviorPortCompileError {
    None,
    InvalidNode,
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
    DisconnectedNode,
    CapacityExceeded,
    BlockedNode,
    IncompatibleBlock,
};

struct BehaviorPortCompileResult {
    std::optional<BehaviorTypedGraphPlan> plan;
    BehaviorPortCompileError error{BehaviorPortCompileError::None};
};

struct BehaviorPortContract {
    std::string_view name;
    BehaviorPortDirection direction{BehaviorPortDirection::Input};
    BehaviorPortDomain domain{BehaviorPortDomain::AudioSignal};
};

struct BehaviorPortContractList {
    std::array<BehaviorPortContract, 4U> ports{};
    std::size_t count{};
};

[[nodiscard]] BehaviorPortContractList BehaviorPortsForKind(BehaviorKind kind) noexcept;

[[nodiscard]] BehaviorPortCompileResult CompileTypedBehaviorGraph(
    const std::vector<BehaviorGraphNodeSpec>& nodes,
    const std::vector<BehaviorPortRouteSpec>& routes);

} // namespace solo_rig
