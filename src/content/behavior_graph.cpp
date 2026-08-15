#include "content/behavior_graph.hpp"

#include <algorithm>

namespace solo_rig {

namespace {

std::optional<std::size_t> FindNode(const std::vector<BehaviorGraphNodeSpec>& nodes,
                                    std::string_view id) {
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (nodes[index].id == id) {
            return index;
        }
    }
    return std::nullopt;
}

bool IsReserved(std::string_view id) noexcept {
    return id == "input" || id == "output";
}

bool IsSingleSignalCompatible(BehaviorKind kind) noexcept {
    return kind == BehaviorKind::Bbd || kind == BehaviorKind::Vco ||
        kind == BehaviorKind::Spring;
}

} // namespace

BehaviorGraphCompileResult CompileBehaviorGraph(
    const std::vector<BehaviorGraphNodeSpec>& nodes,
    const std::vector<BehaviorGraphRouteSpec>& routes) {
    if (nodes.empty() || nodes.size() > kMaximumBehaviorGraphNodes ||
        routes.size() > kMaximumBehaviorGraphRoutes) {
        return {.error = BehaviorGraphError::CapacityExceeded};
    }

    BehaviorGraphPlan candidate;
    candidate.routes = routes;
    candidate.nodes.reserve(nodes.size());
    for (const auto& node : nodes) {
        if (node.id.empty() || IsReserved(node.id) || !node.binding.module_id.size() ||
            FindNode(nodes, node.id).value_or(0U) != candidate.nodes.size()) {
            return {.error = node.id.empty() || node.binding.module_id.empty()
                ? BehaviorGraphError::InvalidNode : BehaviorGraphError::DuplicateNode};
        }
        const auto lowered = LowerBehaviorModule(node.binding);
        if (!lowered.plan.has_value() || lowered.error.has_value()) {
            return {.error = BehaviorGraphError::BlockedNode};
        }
        if (!IsSingleSignalCompatible(lowered.plan->kind)) {
            return {.error = BehaviorGraphError::IncompatibleBlock};
        }
        candidate.nodes.push_back({.id = node.id, .lowering = *lowered.plan});
    }

    std::vector<std::size_t> incoming(nodes.size(), 0U);
    std::vector<std::size_t> outgoing(nodes.size(), 0U);
    std::vector<std::size_t> indegree(nodes.size(), 0U);
    for (std::size_t route_index = 0U; route_index < routes.size(); ++route_index) {
        const auto& route = routes[route_index];
        if (route.source.empty() || route.destination.empty() ||
            route.source == route.destination || route.source == "output" ||
            route.destination == "input") {
            return {.error = BehaviorGraphError::InvalidRoute};
        }
        for (std::size_t previous = 0U; previous < route_index; ++previous) {
            if (routes[previous].source == route.source &&
                routes[previous].destination == route.destination) {
                return {.error = BehaviorGraphError::DuplicateRoute};
            }
        }
        const auto source_node = route.source == "input" ? std::nullopt :
            FindNode(nodes, route.source);
        const auto destination_node = route.destination == "output" ? std::nullopt :
            FindNode(nodes, route.destination);
        if (route.source != "input" && !source_node.has_value()) {
            return {.error = BehaviorGraphError::UnknownEndpoint};
        }
        if (route.destination != "output" && !destination_node.has_value()) {
            return {.error = BehaviorGraphError::UnknownEndpoint};
        }
        if (source_node.has_value() && ++outgoing[*source_node] > 1U) {
            return {.error = BehaviorGraphError::FanOutUnsupported};
        }
        if (destination_node.has_value()) {
            if (++incoming[*destination_node] > 1U) {
                return {.error = BehaviorGraphError::MultipleSignalInputs};
            }
            if (source_node.has_value()) {
                ++indegree[*destination_node];
            }
        }
    }
    std::vector<std::size_t> ready;
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (indegree[index] == 0U) {
            ready.push_back(index);
        }
    }
    while (!ready.empty()) {
        const auto index = ready.front();
        ready.erase(ready.begin());
        candidate.topological_order.push_back(nodes[index].id);
        for (const auto& route : routes) {
            if (route.source != nodes[index].id || route.destination == "output") {
                continue;
            }
            const auto destination = FindNode(nodes, route.destination);
            if (destination.has_value() && --indegree[*destination] == 0U) {
                ready.push_back(*destination);
            }
        }
    }
    if (candidate.topological_order.size() != nodes.size()) {
        return {.error = BehaviorGraphError::CycleDetected};
    }
    const auto output_route = std::count_if(routes.begin(), routes.end(),
        [](const auto& route) { return route.destination == "output"; });
    if (output_route != 1) {
        return {.error = BehaviorGraphError::MissingOutputRoute};
    }
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (incoming[index] == 0U) {
            return {.error = BehaviorGraphError::MissingInputRoute};
        }
    }
    std::vector<BehaviorGraphPlanNode> ordered_nodes;
    ordered_nodes.reserve(candidate.nodes.size());
    for (const auto& id : candidate.topological_order) {
        const auto node = std::find_if(candidate.nodes.begin(), candidate.nodes.end(),
            [&id](const auto& item) { return item.id == id; });
        ordered_nodes.push_back(*node);
    }
    candidate.nodes = std::move(ordered_nodes);
    return {.plan = std::move(candidate)};
}

} // namespace solo_rig
