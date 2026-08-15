#include "content/behavior_clocked.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>
#include <vector>

namespace solo_rig {

namespace {

enum class PortDirection { Input, Output };
enum class PortDomain { Audio, Control };

struct Port {
    std::string_view name;
    PortDirection direction;
    PortDomain domain;
};

constexpr std::size_t kMaximumNodes = 2U;
constexpr std::size_t kMaximumRoutes = 3U;

std::optional<std::size_t> FindNode(const std::vector<BehaviorGraphNodeSpec>& nodes,
                                    std::string_view id) {
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (nodes[index].id == id) return index;
    }
    return std::nullopt;
}

bool IsReserved(std::string_view id) noexcept {
    return id == "input" || id == "output";
}

std::optional<Port> SourcePort(std::string_view endpoint, std::string_view port,
                               std::string_view vco_id, std::string_view bbd_id) noexcept {
    if (endpoint == "input") {
        if (port != "audio") return std::nullopt;
        return Port{port, PortDirection::Output, PortDomain::Audio};
    }
    if (endpoint == vco_id) {
        if (port != "out") return std::nullopt;
        return Port{port, PortDirection::Output, PortDomain::Control};
    }
    if (endpoint == bbd_id) {
        if (port != "out") return std::nullopt;
        return Port{port, PortDirection::Output, PortDomain::Audio};
    }
    return std::nullopt;
}

std::optional<Port> DestinationPort(std::string_view endpoint, std::string_view port,
                                     std::string_view vco_id,
                                     std::string_view bbd_id) noexcept {
    if (endpoint == "output") {
        if (port != "audio") return std::nullopt;
        return Port{port, PortDirection::Input, PortDomain::Audio};
    }
    if (endpoint == bbd_id) {
        if (port == "audio_in") {
            return Port{port, PortDirection::Input, PortDomain::Audio};
        }
        if (port == "clock") {
            return Port{port, PortDirection::Input, PortDomain::Control};
        }
        return std::nullopt;
    }
    if (endpoint == vco_id) return std::nullopt;
    return std::nullopt;
}

bool IsExpectedRoute(const BehaviorPortRouteSpec& route, std::string_view vco_id,
                     std::string_view bbd_id) noexcept {
    return (route.source == "input" && route.source_port == "audio" &&
            route.destination == bbd_id && route.destination_port == "audio_in") ||
        (route.source == vco_id && route.source_port == "out" &&
            route.destination == bbd_id && route.destination_port == "clock") ||
        (route.source == bbd_id && route.source_port == "out" &&
            route.destination == "output" && route.destination_port == "audio");
}

} // namespace

BehaviorClockedCompileResult CompileVcoBbdClockedGraph(
    const std::vector<BehaviorGraphNodeSpec>& nodes,
    const std::vector<BehaviorPortRouteSpec>& routes) {
    if (nodes.empty() || nodes.size() > kMaximumNodes || routes.size() > kMaximumRoutes) {
        return {.error = BehaviorClockedCompileError::CapacityExceeded};
    }
    if (nodes.size() != kMaximumNodes || routes.size() != kMaximumRoutes) {
        return {.error = BehaviorClockedCompileError::InvalidPlan};
    }

    BehaviorClockedGraphPlan candidate;
    candidate.routes = routes;
    candidate.nodes.reserve(nodes.size());
    std::string vco_id;
    std::string bbd_id;
    for (const auto& node : nodes) {
        if (node.id.empty() || IsReserved(node.id) || node.binding.module_id.empty()) {
            return {.error = BehaviorClockedCompileError::InvalidPlan};
        }
        if (FindNode(nodes, node.id).value_or(0U) != candidate.nodes.size()) {
            return {.error = BehaviorClockedCompileError::DuplicateNode};
        }
        const auto lowered = LowerBehaviorModule(node.binding);
        if (!lowered.plan.has_value() || lowered.error.has_value()) {
            return {.error = BehaviorClockedCompileError::UnsupportedBlock};
        }
        if (lowered.plan->status != WhiteboxPlanStatus::Ready) {
            return {.error = BehaviorClockedCompileError::UnsupportedBlock};
        }
        if (lowered.plan->kind == BehaviorKind::Vco &&
            lowered.plan->model_id == "generic-vco-v1" && vco_id.empty()) {
            vco_id = node.id;
        } else if (lowered.plan->kind == BehaviorKind::Bbd &&
                   lowered.plan->model_id == "generic-bbd-v1" && bbd_id.empty()) {
            bbd_id = node.id;
        } else {
            return {.error = BehaviorClockedCompileError::UnsupportedBlock};
        }
        candidate.nodes.push_back({.id = node.id, .lowering = *lowered.plan});
    }
    if (vco_id.empty() || bbd_id.empty()) {
        return {.error = BehaviorClockedCompileError::UnsupportedBlock};
    }

    bool audio_seen = false;
    bool clock_seen = false;
    bool output_seen = false;
    std::size_t internal_indegree = 0U;
    for (std::size_t route_index = 0U; route_index < routes.size(); ++route_index) {
        const auto& route = routes[route_index];
        if (route.source.empty() || route.source_port.empty() || route.destination.empty() ||
            route.destination_port.empty() || route.source == route.destination) {
            return {.error = BehaviorClockedCompileError::InvalidRoute};
        }
        for (std::size_t previous = 0U; previous < route_index; ++previous) {
            if (routes[previous].source == route.source &&
                routes[previous].source_port == route.source_port &&
                routes[previous].destination == route.destination &&
                routes[previous].destination_port == route.destination_port) {
                return {.error = BehaviorClockedCompileError::DuplicateRoute};
            }
        }
        const bool source_known = route.source == "input" || route.source == vco_id ||
            route.source == bbd_id;
        const bool destination_known = route.destination == "output" ||
            route.destination == vco_id || route.destination == bbd_id;
        if (!source_known || !destination_known) {
            return {.error = BehaviorClockedCompileError::UnknownEndpoint};
        }
        const auto source = SourcePort(route.source, route.source_port, vco_id, bbd_id);
        const auto destination = DestinationPort(route.destination, route.destination_port,
            vco_id, bbd_id);
        if (!source.has_value() || !destination.has_value()) {
            return {.error = BehaviorClockedCompileError::UnknownPort};
        }
        if (source->direction != PortDirection::Output ||
            destination->direction != PortDirection::Input) {
            return {.error = BehaviorClockedCompileError::DirectionMismatch};
        }
        if (source->domain != destination->domain) {
            return {.error = BehaviorClockedCompileError::IncompatibleConnection};
        }
        if (!IsExpectedRoute(route, vco_id, bbd_id)) {
            return {.error = BehaviorClockedCompileError::IncompatibleConnection};
        }
        if (route.source == "input") {
            if (audio_seen) return {.error = BehaviorClockedCompileError::FanOutUnsupported};
            audio_seen = true;
        } else if (route.source == vco_id) {
            if (clock_seen) return {.error = BehaviorClockedCompileError::FanOutUnsupported};
            clock_seen = true;
            ++internal_indegree;
        } else {
            if (output_seen) return {.error = BehaviorClockedCompileError::FanOutUnsupported};
            output_seen = true;
        }
    }
    if (!audio_seen || !clock_seen || !output_seen || internal_indegree != 1U) {
        return {.error = BehaviorClockedCompileError::MissingPort};
    }
    candidate.topological_order = {vco_id, bbd_id};
    std::vector<BehaviorGraphPlanNode> ordered;
    ordered.reserve(candidate.nodes.size());
    for (const auto& id : candidate.topological_order) {
        const auto node = std::find_if(candidate.nodes.begin(), candidate.nodes.end(),
            [&id](const auto& item) { return item.id == id; });
        if (node == candidate.nodes.end()) {
            return {.error = BehaviorClockedCompileError::InvalidPlan};
        }
        ordered.push_back(*node);
    }
    candidate.nodes = std::move(ordered);
    return {.plan = std::move(candidate)};
}

} // namespace solo_rig
