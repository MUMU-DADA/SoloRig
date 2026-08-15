#include "content/behavior_ports.hpp"

#include <algorithm>
#include <initializer_list>

namespace solo_rig {

namespace {

constexpr std::size_t kMaximumTypedPorts = 4U;

BehaviorPortContractList Ports(std::initializer_list<BehaviorPortContract> ports) noexcept {
    BehaviorPortContractList result{};
    result.count = ports.size();
    std::size_t index = 0U;
    for (const auto port : ports) result.ports[index++] = port;
    return result;
}

bool IsReserved(std::string_view id) noexcept {
    return id == "input" || id == "output";
}

std::optional<std::size_t> FindNode(const std::vector<BehaviorGraphNodeSpec>& nodes,
                                    std::string_view id) {
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (nodes[index].id == id) return index;
    }
    return std::nullopt;
}

const BehaviorPortContract* FindPort(const BehaviorPortContractList& ports,
                                     std::string_view name) noexcept {
    for (std::size_t index = 0U; index < ports.count; ++index) {
        if (ports.ports[index].name == name) return &ports.ports[index];
    }
    return nullptr;
}

std::optional<std::size_t> BoundaryInputIndex(std::string_view name) noexcept {
    if (name == "audio") return 0U;
    if (name == "control") return 1U;
    if (name == "detector") return 2U;
    if (name == "emitter") return 3U;
    return std::nullopt;
}

std::optional<std::size_t> BoundaryOutputIndex(std::string_view name) noexcept {
    if (name == "audio") return 0U;
    if (name == "control") return 1U;
    if (name == "resistance") return 2U;
    return std::nullopt;
}

bool Compatible(BehaviorPortDomain source, BehaviorPortDomain destination) noexcept {
    if (source == destination) return true;
    return source == BehaviorPortDomain::AudioSignal &&
        (destination == BehaviorPortDomain::DetectorInput ||
         destination == BehaviorPortDomain::EmitterDrive);
}

} // namespace

BehaviorPortContractList BehaviorPortsForKind(BehaviorKind kind) noexcept {
    using D = BehaviorPortDirection;
    using T = BehaviorPortDomain;
    switch (kind) {
    case BehaviorKind::Bbd:
        return Ports({{"in", D::Input, T::AudioSignal}, {"out", D::Output, T::AudioSignal}});
    case BehaviorKind::Vco:
        return Ports({{"control", D::Input, T::ControlSignal},
            {"out", D::Output, T::AudioSignal}});
    case BehaviorKind::Vca:
        return Ports({{"audio_in", D::Input, T::AudioSignal},
            {"control", D::Input, T::ControlSignal}, {"out", D::Output, T::AudioSignal}});
    case BehaviorKind::Envelope:
        return Ports({{"detector", D::Input, T::DetectorInput},
            {"out", D::Output, T::ControlSignal}});
    case BehaviorKind::Lfo:
        return Ports({{"out", D::Output, T::ControlSignal}});
    case BehaviorKind::Optocoupler:
        return Ports({{"emitter", D::Input, T::EmitterDrive},
            {"out", D::Output, T::Resistance}});
    case BehaviorKind::Spring:
        return Ports({{"in", D::Input, T::AudioSignal}, {"out", D::Output, T::AudioSignal}});
    }
    return {};
}

BehaviorPortCompileResult CompileTypedBehaviorGraph(
    const std::vector<BehaviorGraphNodeSpec>& nodes,
    const std::vector<BehaviorPortRouteSpec>& routes) {
    if (nodes.empty() || nodes.size() > kMaximumBehaviorGraphNodes ||
        routes.size() > kMaximumBehaviorGraphRoutes) {
        return {.error = BehaviorPortCompileError::CapacityExceeded};
    }
    BehaviorTypedGraphPlan candidate;
    candidate.routes = routes;
    candidate.nodes.reserve(nodes.size());
    for (const auto& node : nodes) {
        if (node.id.empty() || IsReserved(node.id) || node.binding.module_id.empty()) {
            return {.error = BehaviorPortCompileError::InvalidNode};
        }
        if (FindNode(nodes, node.id).value_or(0U) != candidate.nodes.size()) {
            return {.error = BehaviorPortCompileError::DuplicateNode};
        }
        const auto lowered = LowerBehaviorModule(node.binding);
        if (!lowered.plan.has_value() || lowered.error.has_value()) {
            return {.error = BehaviorPortCompileError::BlockedNode};
        }
        candidate.nodes.push_back({node.id, *lowered.plan});
    }

    std::vector<std::size_t> indegree(nodes.size(), 0U);
    std::vector<std::size_t> incoming(nodes.size() * kMaximumTypedPorts, 0U);
    std::vector<std::size_t> outgoing(nodes.size() * kMaximumTypedPorts, 0U);
    std::array<bool, 4U> external_input_seen{};
    std::array<bool, 3U> external_output_seen{};
    for (std::size_t route_index = 0U; route_index < routes.size(); ++route_index) {
        const auto& route = routes[route_index];
        if (route.source.empty() || route.source_port.empty() || route.destination.empty() ||
            route.destination_port.empty() || route.source == route.destination) {
            return {.error = BehaviorPortCompileError::InvalidRoute};
        }
        for (std::size_t previous = 0U; previous < route_index; ++previous) {
            const auto& prior = routes[previous];
            if (prior.source == route.source && prior.source_port == route.source_port &&
                prior.destination == route.destination &&
                prior.destination_port == route.destination_port) {
                return {.error = BehaviorPortCompileError::DuplicateRoute};
            }
        }

        const auto source_index = route.source == "input" ? std::nullopt :
            FindNode(nodes, route.source);
        const auto destination_index = route.destination == "output" ? std::nullopt :
            FindNode(nodes, route.destination);
        if (route.source != "input" && !source_index.has_value()) {
            return {.error = BehaviorPortCompileError::UnknownEndpoint};
        }
        if (route.destination != "output" && !destination_index.has_value()) {
            return {.error = BehaviorPortCompileError::UnknownEndpoint};
        }

        BehaviorPortContract source_port{};
        BehaviorPortContract destination_port{};
        if (route.source == "input") {
            const auto input_index = BoundaryInputIndex(route.source_port);
            if (!input_index.has_value()) return {.error = BehaviorPortCompileError::UnknownPort};
            const auto domain = route.source_port == "audio" ? BehaviorPortDomain::AudioSignal :
                route.source_port == "control" ? BehaviorPortDomain::ControlSignal :
                route.source_port == "detector" ? BehaviorPortDomain::DetectorInput :
                    BehaviorPortDomain::EmitterDrive;
            source_port = {route.source_port, BehaviorPortDirection::Output, domain};
            if (external_input_seen[*input_index]) {
                return {.error = BehaviorPortCompileError::FanOutUnsupported};
            }
            external_input_seen[*input_index] = true;
        } else {
            const auto ports = BehaviorPortsForKind(candidate.nodes[*source_index].lowering.kind);
            const auto* found = FindPort(ports, route.source_port);
            if (found == nullptr) return {.error = BehaviorPortCompileError::UnknownPort};
            source_port = *found;
            if (source_port.direction != BehaviorPortDirection::Output) {
                return {.error = BehaviorPortCompileError::DirectionMismatch};
            }
            if (outgoing[*source_index * kMaximumTypedPorts +
                         static_cast<std::size_t>(found - ports.ports.data())]++ != 0U) {
                return {.error = BehaviorPortCompileError::FanOutUnsupported};
            }
        }
        if (route.destination == "output") {
            const auto output_index = BoundaryOutputIndex(route.destination_port);
            if (!output_index.has_value()) {
                return {.error = BehaviorPortCompileError::UnknownPort};
            }
            destination_port = {route.destination_port, BehaviorPortDirection::Input,
                route.destination_port == "audio" ? BehaviorPortDomain::AudioSignal :
                route.destination_port == "control" ? BehaviorPortDomain::ControlSignal :
                    BehaviorPortDomain::Resistance};
            if (external_output_seen[*output_index]) {
                return {.error = BehaviorPortCompileError::FanInUnsupported};
            }
            external_output_seen[*output_index] = true;
        } else {
            const auto ports = BehaviorPortsForKind(candidate.nodes[*destination_index].lowering.kind);
            const auto* found = FindPort(ports, route.destination_port);
            if (found == nullptr) return {.error = BehaviorPortCompileError::UnknownPort};
            destination_port = *found;
            if (destination_port.direction != BehaviorPortDirection::Input) {
                return {.error = BehaviorPortCompileError::DirectionMismatch};
            }
            if (incoming[*destination_index * kMaximumTypedPorts +
                         static_cast<std::size_t>(found - ports.ports.data())]++ != 0U) {
                return {.error = BehaviorPortCompileError::FanInUnsupported};
            }
            if (source_index.has_value()) ++indegree[*destination_index];
        }
        if (!Compatible(source_port.domain, destination_port.domain)) {
            return {.error = BehaviorPortCompileError::IncompatibleConnection};
        }
    }

    const auto has_external_input = std::any_of(external_input_seen.begin(),
                                                external_input_seen.end(),
                                                [](bool seen) { return seen; });
    const auto has_external_output = std::any_of(external_output_seen.begin(),
                                                 external_output_seen.end(),
                                                 [](bool seen) { return seen; });
    if (!has_external_input || !has_external_output) {
        return {.error = BehaviorPortCompileError::DisconnectedNode};
    }
    std::vector<std::size_t> ready;
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (indegree[index] == 0U) ready.push_back(index);
    }
    while (!ready.empty()) {
        const auto index = ready.front();
        ready.erase(ready.begin());
        candidate.topological_order.push_back(nodes[index].id);
        for (const auto& route : routes) {
            if (route.source == "input" || route.destination == "output" ||
                route.source != nodes[index].id) continue;
            const auto destination = FindNode(nodes, route.destination);
            if (destination.has_value() && --indegree[*destination] == 0U) ready.push_back(*destination);
        }
    }
    if (candidate.topological_order.size() != nodes.size()) {
        return {.error = BehaviorPortCompileError::CycleDetected};
    }

    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        const auto ports = BehaviorPortsForKind(candidate.nodes[index].lowering.kind);
        for (std::size_t port = 0U; port < ports.count; ++port) {
            const auto& contract = ports.ports[port];
            const auto count = contract.direction == BehaviorPortDirection::Input ?
                incoming[index * kMaximumTypedPorts + port] :
                outgoing[index * kMaximumTypedPorts + port];
            if (count != 1U) return {.error = BehaviorPortCompileError::MissingPort};
        }
    }
    std::vector<BehaviorGraphPlanNode> ordered;
    ordered.reserve(candidate.nodes.size());
    for (const auto& id : candidate.topological_order) {
        const auto node = std::find_if(candidate.nodes.begin(), candidate.nodes.end(),
            [&id](const auto& item) { return item.id == id; });
        ordered.push_back(*node);
    }
    candidate.nodes = std::move(ordered);
    return {.plan = std::move(candidate)};
}

} // namespace solo_rig
