#include "content/topology_decomposition.hpp"

#include "content/json.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace solo_rig {

namespace {

WhiteboxDecompositionResult Failure(WhiteboxDecompositionError error,
                                    WhiteboxTopologyClass classification) {
    return {.plan = std::nullopt, .error = error, .classification = classification};
}

bool HasCycle(const JsonValue::Object& object) {
    const auto components_value = object.find("components");
    if (components_value == object.end() || components_value->second.AsArray() == nullptr) {
        return false;
    }
    std::unordered_map<std::string, std::vector<std::string>> adjacency;
    for (const auto& value : *components_value->second.AsArray()) {
        const auto* component = value.AsObject();
        if (component == nullptr) {
            continue;
        }
        const auto pins_value = component->find("pins");
        if (pins_value == component->end() || pins_value->second.AsArray() == nullptr ||
            pins_value->second.AsArray()->size() != 2U) {
            continue;
        }
        const auto* first = (*pins_value->second.AsArray())[0].AsString();
        const auto* second = (*pins_value->second.AsArray())[1].AsString();
        if (first == nullptr || second == nullptr || *first == *second) {
            continue;
        }
        adjacency[*first].push_back(*second);
        adjacency[*second].push_back(*first);
    }
    std::unordered_set<std::string> visited;
    std::function<bool(std::string_view, std::string_view)> visit =
        [&](std::string_view node, std::string_view parent) {
            visited.insert(std::string(node));
            for (const auto& neighbour : adjacency[std::string(node)]) {
                if (!visited.contains(neighbour)) {
                    if (visit(neighbour, node)) {
                        return true;
                    }
                } else if (neighbour != parent) {
                    return true;
                }
            }
            return false;
        };
    for (const auto& [node, neighbours] : adjacency) {
        static_cast<void>(neighbours);
        if (!visited.contains(node) && visit(node, "")) {
            return true;
        }
    }
    return false;
}

bool HasSubcircuits(const JsonValue::Object& object) {
    const auto iterator = object.find("subcircuits");
    return iterator != object.end() && iterator->second.AsArray() != nullptr &&
           !iterator->second.AsArray()->empty();
}

} // namespace

WhiteboxStageRuntimeError WhiteboxStageRuntime::Process(double input, double& output) noexcept {
    output = 0.0;
    if (!prepared_) {
        return WhiteboxStageRuntimeError::NotPrepared;
    }
    return std::isfinite(input) ? WhiteboxStageRuntimeError::UnsupportedAssembly
                                : WhiteboxStageRuntimeError::NonFiniteInput;
}

WhiteboxDecompositionResult AnalyzeWhiteboxGraph(const WhiteboxCompiledGraph& graph) {
    if (graph.Classification() != WhiteboxTopologyClass::Series &&
        graph.Classification() != WhiteboxTopologyClass::Parallel) {
        return Failure(WhiteboxDecompositionError::UnsupportedTopology, graph.Classification());
    }
    WhiteboxExecutionPlan plan;
    plan.classification = graph.Classification();
    WhiteboxPartitionIr partition{.id = "partition-0", .classification = graph.Classification()};
    std::vector<const WhiteboxComponent*> components;
    components.reserve(graph.Components().size());
    for (const auto& component : graph.Components()) {
        components.push_back(&component);
        partition.component_ids.push_back(component.id);
    }
    std::sort(components.begin(), components.end(), [](const auto* left, const auto* right) {
        return left->id < right->id;
    });
    std::sort(partition.component_ids.begin(), partition.component_ids.end());
    for (const auto& port : graph.Ports()) {
        if (std::find(partition.boundary_nodes.begin(), partition.boundary_nodes.end(), port.node) ==
            partition.boundary_nodes.end()) {
            partition.boundary_nodes.push_back(port.node);
        }
    }
    std::sort(partition.boundary_nodes.begin(), partition.boundary_nodes.end());
    plan.partitions.push_back(std::move(partition));

    for (const auto* component : components) {
        WhiteboxStageIr stage{.id = "stage-" + component->id,
                              .component_ids = {component->id},
                              .status = WhiteboxPlanStatus::Planned,
                              .feedback = false};
        if (component->pins.size() >= 2U) {
            stage.input_nodes.push_back(component->pins[0]);
            stage.output_nodes.push_back(component->pins[1]);
        }
        plan.signal_order.push_back(stage.id);
        plan.stages.push_back(std::move(stage));
    }
    plan.feedback = {.status = WhiteboxPlanStatus::Planned, .detected = false};
    plan.blockwise.stage_ids = plan.signal_order;
    plan.blockwise.offsets.reserve(plan.signal_order.size() + 1U);
    for (std::uint32_t index = 0; index <= plan.signal_order.size(); ++index) {
        plan.blockwise.offsets.push_back(index);
    }
    plan.k_method.coefficients.assign(plan.signal_order.size(), 1.0);
    plan.assembly.stage_ids = plan.signal_order;
    return {.plan = std::move(plan), .error = std::nullopt, .classification = graph.Classification()};
}

WhiteboxDecompositionResult AnalyzeWhiteboxGraphV2Json(std::string_view source) {
    const auto compiled = CompileWhiteboxGraphV2Json(source);
    if (compiled.graph.has_value()) {
        return AnalyzeWhiteboxGraph(*compiled.graph);
    }
    if (compiled.error == WhiteboxCompileError::UnsupportedTopology) {
        const auto parsed = ParseJson(source);
        if (parsed.value.has_value() && parsed.value->AsObject() != nullptr) {
            const auto& object = *parsed.value->AsObject();
            if (HasSubcircuits(object)) {
                return Failure(WhiteboxDecompositionError::CrossNetworkBoundary, compiled.classification);
            }
            if (compiled.classification == WhiteboxTopologyClass::Spqr && HasCycle(object)) {
                return Failure(WhiteboxDecompositionError::FeedbackLoop, compiled.classification);
            }
        }
        return Failure(WhiteboxDecompositionError::NonDecomposable, compiled.classification);
    }
    return Failure(WhiteboxDecompositionError::InvalidGraph, compiled.classification);
}

WhiteboxStageRuntimePrepareResult PrepareWhiteboxStageRuntime(const WhiteboxExecutionPlan& plan) {
    static_cast<void>(plan);
    // M15 does not yet own a stage solver or behavior runtime; refusing here
    // prevents a planned table from being mistaken for an audio processor.
    return {.runtime = std::nullopt, .error = WhiteboxStageRuntimeError::UnsupportedAssembly};
}

} // namespace solo_rig
