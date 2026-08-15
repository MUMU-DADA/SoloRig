#include "content/behavior_composite.hpp"

#include <array>
#include <string_view>
#include <utility>

namespace solo_rig {

namespace {

constexpr std::size_t kCompositeNodeCount = 2U;
constexpr std::size_t kCompositeRouteCount = 4U;

bool IsReserved(std::string_view id) noexcept {
    return id == "input" || id == "output";
}

bool IsExpectedRoute(const BehaviorPortRouteSpec& route, std::string_view envelope_id,
                    std::string_view vca_id, std::size_t index) noexcept {
    switch (index) {
    case 0U:
        return route.source == "input" && route.source_port == "audio" &&
            route.destination == vca_id && route.destination_port == "audio_in";
    case 1U:
        return route.source == "input" && route.source_port == "detector" &&
            route.destination == envelope_id && route.destination_port == "detector";
    case 2U:
        return route.source == envelope_id && route.source_port == "out" &&
            route.destination == vca_id && route.destination_port == "control";
    case 3U:
        return route.source == vca_id && route.source_port == "out" &&
            route.destination == "output" && route.destination_port == "audio";
    default:
        return false;
    }
}

} // namespace

BehaviorPortCompileResult CompileEnvelopeVcaComposite(
    const std::vector<BehaviorGraphNodeSpec>& nodes,
    const std::vector<BehaviorPortRouteSpec>& routes) {
    for (std::size_t index = 0U; index < routes.size(); ++index) {
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (routes[prior].source == routes[index].source &&
                routes[prior].source_port == routes[index].source_port &&
                routes[prior].destination == routes[index].destination &&
                routes[prior].destination_port == routes[index].destination_port) {
                return {.error = BehaviorPortCompileError::DuplicateRoute};
            }
        }
    }
    std::size_t external_audio_routes = 0U;
    std::size_t external_detector_routes = 0U;
    for (const auto& route : routes) {
        if (route.source == "input" && route.source_port == "audio") {
            ++external_audio_routes;
        } else if (route.source == "input" && route.source_port == "detector") {
            ++external_detector_routes;
        }
    }
    if (external_audio_routes > 1U || external_detector_routes > 1U) {
        return {.error = BehaviorPortCompileError::FanOutUnsupported};
    }
    if (nodes.size() != kCompositeNodeCount) {
        return {.error = nodes.size() > kCompositeNodeCount ?
            BehaviorPortCompileError::CapacityExceeded : BehaviorPortCompileError::InvalidNode};
    }
    if (routes.size() > kCompositeRouteCount) {
        return {.error = BehaviorPortCompileError::CapacityExceeded};
    }

    std::size_t envelope_index = kCompositeNodeCount;
    std::size_t vca_index = kCompositeNodeCount;
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        const auto& node = nodes[index];
        if (node.id.empty() || IsReserved(node.id) || node.binding.module_id.empty()) {
            return {.error = BehaviorPortCompileError::InvalidNode};
        }
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (nodes[prior].id == node.id) {
                return {.error = BehaviorPortCompileError::DuplicateNode};
            }
        }
        const auto lowered = LowerBehaviorModule(node.binding);
        if (!lowered.plan.has_value() || lowered.error.has_value()) {
            return {.error = BehaviorPortCompileError::BlockedNode};
        }
        if (lowered.plan->status != WhiteboxPlanStatus::Ready) {
            return {.error = BehaviorPortCompileError::IncompatibleBlock};
        }
        if (lowered.plan->kind == BehaviorKind::Envelope &&
            lowered.plan->model_id == "generic-envelope-v1" && envelope_index == kCompositeNodeCount) {
            envelope_index = index;
        } else if (lowered.plan->kind == BehaviorKind::Vca &&
                   lowered.plan->model_id == "generic-vca-v1" && vca_index == kCompositeNodeCount) {
            vca_index = index;
        } else {
            return {.error = BehaviorPortCompileError::IncompatibleBlock};
        }
    }
    if (envelope_index == kCompositeNodeCount || vca_index == kCompositeNodeCount) {
        return {.error = BehaviorPortCompileError::IncompatibleBlock};
    }

    const auto envelope_id = nodes[envelope_index].id;
    const auto vca_id = nodes[vca_index].id;
    std::array<bool, kCompositeRouteCount> expected_seen{};
    for (const auto& route : routes) {
        if (route.source.empty() || route.source_port.empty() || route.destination.empty() ||
            route.destination_port.empty() || route.source == route.destination) {
            return {.error = BehaviorPortCompileError::InvalidRoute};
        }
        std::size_t matched = kCompositeRouteCount;
        for (std::size_t index = 0U; index < kCompositeRouteCount; ++index) {
            if (IsExpectedRoute(route, envelope_id, vca_id, index)) {
                matched = index;
                break;
            }
        }
        if (matched == kCompositeRouteCount) {
            return {.error = BehaviorPortCompileError::IncompatibleConnection};
        }
        if (expected_seen[matched]) {
            return {.error = BehaviorPortCompileError::DuplicateRoute};
        }
        expected_seen[matched] = true;
    }
    if (routes.size() != kCompositeRouteCount || external_audio_routes != 1U ||
        external_detector_routes != 1U) {
        return {.error = BehaviorPortCompileError::MissingPort};
    }
    for (const bool seen : expected_seen) {
        if (!seen) return {.error = BehaviorPortCompileError::MissingPort};
    }

    BehaviorTypedGraphPlan candidate;
    candidate.nodes.reserve(kCompositeNodeCount);
    candidate.nodes.push_back({envelope_id, *LowerBehaviorModule(nodes[envelope_index].binding).plan});
    candidate.nodes.push_back({vca_id, *LowerBehaviorModule(nodes[vca_index].binding).plan});
    candidate.routes = routes;
    candidate.topological_order = {envelope_id, vca_id};
    return {.plan = std::move(candidate)};
}

} // namespace solo_rig
