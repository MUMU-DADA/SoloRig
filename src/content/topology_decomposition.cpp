#include "content/topology_decomposition.hpp"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <map>
#include <memory>
#include <numeric>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace solo_rig {

namespace {

constexpr std::size_t kMaximumStageComponents = 256U;

struct DecompositionNode {
    WhiteboxStageKind kind{WhiteboxStageKind::Q};
    std::string input_node;
    std::string output_node;
    std::string key;
    std::vector<std::string> component_ids;
    std::vector<std::shared_ptr<DecompositionNode>> children;
};

struct ReductionEdge {
    std::string first;
    std::string second;
    std::shared_ptr<DecompositionNode> node;
};

WhiteboxDecompositionResult Failure(WhiteboxDecompositionError error,
                                    WhiteboxTopologyClass classification) {
    return {.plan = std::nullopt, .error = error, .classification = classification};
}

std::pair<std::string, std::string> OrderedPair(std::string first, std::string second) {
    if (second < first) {
        std::swap(first, second);
    }
    return {std::move(first), std::move(second)};
}

std::string PairKey(std::string_view first, std::string_view second) {
    const auto pair = OrderedPair(std::string(first), std::string(second));
    return pair.first + "\n" + pair.second;
}

void SortUnique(std::vector<std::string>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

std::vector<std::string> ComponentUnion(
    const std::vector<std::shared_ptr<DecompositionNode>>& children) {
    std::vector<std::string> ids;
    for (const auto& child : children) {
        ids.insert(ids.end(), child->component_ids.begin(), child->component_ids.end());
    }
    SortUnique(ids);
    return ids;
}

std::string CompositeKey(char prefix,
                         const std::vector<std::shared_ptr<DecompositionNode>>& children) {
    std::string key(1U, prefix);
    key.push_back('(');
    for (const auto& child : children) {
        key += std::to_string(child->key.size());
        key.push_back(':');
        key += child->key;
    }
    key.push_back(')');
    return key;
}

bool Touches(const DecompositionNode& node, std::string_view endpoint) {
    return node.input_node == endpoint || node.output_node == endpoint;
}

std::optional<std::string> SharedEndpoint(const DecompositionNode& first,
                                          const DecompositionNode& second) {
    for (const auto* candidate : {&first.input_node, &first.output_node}) {
        if (Touches(second, *candidate)) {
            return *candidate;
        }
    }
    return std::nullopt;
}

void Orient(const std::shared_ptr<DecompositionNode>& node,
            std::string_view input,
            std::string_view output) {
    if (node->kind == WhiteboxStageKind::Series && node->children.size() == 2U) {
        const auto middle = SharedEndpoint(*node->children[0], *node->children[1]);
        if (middle.has_value()) {
            if (!Touches(*node->children[0], input)) {
                std::swap(node->children[0], node->children[1]);
            }
            Orient(node->children[0], input, *middle);
            Orient(node->children[1], *middle, output);
        }
    } else if (node->kind == WhiteboxStageKind::Parallel) {
        for (auto& child : node->children) {
            Orient(child, input, output);
        }
    }
    node->input_node = std::string(input);
    node->output_node = std::string(output);
}

std::optional<std::string> PortNode(const WhiteboxCompiledGraph& graph,
                                    WhiteboxPortDirection direction) {
    const auto iterator = std::find_if(graph.Ports().begin(), graph.Ports().end(),
                                       [direction](const auto& port) {
                                           return port.direction == direction;
                                       });
    return iterator == graph.Ports().end() ? std::nullopt
                                           : std::optional<std::string>(iterator->node);
}

bool Connected(const WhiteboxCompiledGraph& graph,
               std::string_view start,
               std::string_view target) {
    std::unordered_map<std::string, std::vector<std::string>> adjacency;
    for (const auto& component : graph.Components()) {
        if (component.pins.size() != 2U) {
            continue;
        }
        adjacency[component.pins[0]].push_back(component.pins[1]);
        adjacency[component.pins[1]].push_back(component.pins[0]);
    }
    std::vector<std::string> pending{std::string(start)};
    std::unordered_set<std::string> visited;
    while (!pending.empty()) {
        auto node = std::move(pending.back());
        pending.pop_back();
        if (!visited.insert(node).second) {
            continue;
        }
        if (node == target) {
            return true;
        }
        const auto iterator = adjacency.find(node);
        if (iterator != adjacency.end()) {
            pending.insert(pending.end(), iterator->second.begin(), iterator->second.end());
        }
    }
    return false;
}

std::optional<std::pair<std::string, std::string>> NetworkTerminals(
    const WhiteboxCompiledGraph& graph) {
    const auto input = PortNode(graph, WhiteboxPortDirection::Input);
    const auto output = PortNode(graph, WhiteboxPortDirection::Output);
    if (!input.has_value() || !output.has_value()) {
        return std::nullopt;
    }
    if (Connected(graph, *input, graph.ReferenceGround())) {
        return std::pair<std::string, std::string>{*input, std::string(graph.ReferenceGround())};
    }
    return std::pair<std::string, std::string>{*input, *output};
}

void RemoveEdges(std::vector<ReductionEdge>& edges,
                 const std::vector<std::size_t>& indices) {
    std::vector<bool> removed(edges.size(), false);
    for (const auto index : indices) {
        removed[index] = true;
    }
    std::vector<ReductionEdge> retained;
    retained.reserve(edges.size() - indices.size());
    for (std::size_t index = 0; index < edges.size(); ++index) {
        if (!removed[index]) {
            retained.push_back(std::move(edges[index]));
        }
    }
    edges = std::move(retained);
}

std::shared_ptr<DecompositionNode> ReduceGraph(const WhiteboxCompiledGraph& graph,
                                               std::string_view input_terminal,
                                               std::string_view output_terminal) {
    std::vector<const WhiteboxComponent*> components;
    components.reserve(graph.Components().size());
    for (const auto& component : graph.Components()) {
        components.push_back(&component);
    }
    std::sort(components.begin(), components.end(), [](const auto* left, const auto* right) {
        return left->id < right->id;
    });

    std::vector<ReductionEdge> edges;
    edges.reserve(components.size());
    for (const auto* component : components) {
        const auto endpoints = OrderedPair(component->pins[0], component->pins[1]);
        auto leaf = std::make_shared<DecompositionNode>();
        leaf->kind = WhiteboxStageKind::Q;
        leaf->input_node = endpoints.first;
        leaf->output_node = endpoints.second;
        leaf->key = "Q(" + std::to_string(component->id.size()) + ":" + component->id + ")";
        leaf->component_ids = {component->id};
        edges.push_back({endpoints.first, endpoints.second, std::move(leaf)});
    }

    while (true) {
        bool reduced = false;
        std::map<std::string, std::vector<std::size_t>> parallel_groups;
        for (std::size_t index = 0; index < edges.size(); ++index) {
            parallel_groups[PairKey(edges[index].first, edges[index].second)].push_back(index);
        }
        for (const auto& [key, indices] : parallel_groups) {
            static_cast<void>(key);
            if (indices.size() < 2U) {
                continue;
            }
            std::vector<std::shared_ptr<DecompositionNode>> children;
            for (const auto index : indices) {
                children.push_back(edges[index].node);
            }
            std::sort(children.begin(), children.end(), [](const auto& left, const auto& right) {
                return left->key < right->key;
            });
            const auto endpoints = OrderedPair(edges[indices[0]].first, edges[indices[0]].second);
            auto parallel = std::make_shared<DecompositionNode>();
            parallel->kind = WhiteboxStageKind::Parallel;
            parallel->children = std::move(children);
            parallel->component_ids = ComponentUnion(parallel->children);
            parallel->key = CompositeKey('P', parallel->children);
            Orient(parallel, endpoints.first, endpoints.second);
            RemoveEdges(edges, indices);
            edges.push_back({endpoints.first, endpoints.second, std::move(parallel)});
            reduced = true;
            break;
        }
        if (reduced) {
            continue;
        }

        std::map<std::string, std::vector<std::size_t>> incident;
        for (std::size_t index = 0; index < edges.size(); ++index) {
            incident[edges[index].first].push_back(index);
            incident[edges[index].second].push_back(index);
        }
        for (const auto& [node, indices] : incident) {
            if (node == input_terminal || node == output_terminal || indices.size() != 2U ||
                indices[0] == indices[1]) {
                continue;
            }
            const auto other = [&node](const ReductionEdge& edge) {
                return edge.first == node ? edge.second : edge.first;
            };
            const auto first_other = other(edges[indices[0]]);
            const auto second_other = other(edges[indices[1]]);
            if (first_other == second_other) {
                continue;
            }
            const auto endpoints = OrderedPair(first_other, second_other);
            auto first_child = edges[indices[0]].node;
            auto second_child = edges[indices[1]].node;
            if (!Touches(*first_child, endpoints.first)) {
                std::swap(first_child, second_child);
            }
            auto series = std::make_shared<DecompositionNode>();
            series->kind = WhiteboxStageKind::Series;
            series->children = {std::move(first_child), std::move(second_child)};
            Orient(series, endpoints.first, endpoints.second);
            series->component_ids = ComponentUnion(series->children);
            series->key = CompositeKey('S', series->children);
            RemoveEdges(edges, indices);
            edges.push_back({endpoints.first, endpoints.second, std::move(series)});
            reduced = true;
            break;
        }
        if (!reduced) {
            break;
        }
    }

    if (edges.size() == 1U &&
        PairKey(edges[0].first, edges[0].second) == PairKey(input_terminal, output_terminal)) {
        Orient(edges[0].node, input_terminal, output_terminal);
        return edges[0].node;
    }

    std::sort(edges.begin(), edges.end(), [](const auto& left, const auto& right) {
        return left.node->key < right.node->key;
    });
    auto rigid = std::make_shared<DecompositionNode>();
    rigid->kind = WhiteboxStageKind::Rigid;
    rigid->input_node = std::string(input_terminal);
    rigid->output_node = std::string(output_terminal);
    for (auto& edge : edges) {
        rigid->children.push_back(std::move(edge.node));
    }
    rigid->component_ids = ComponentUnion(rigid->children);
    rigid->key = CompositeKey('R', rigid->children);
    return rigid;
}

WhiteboxTopologyClass TopologyFor(WhiteboxStageKind kind) {
    switch (kind) {
    case WhiteboxStageKind::Series:
        return WhiteboxTopologyClass::Series;
    case WhiteboxStageKind::Parallel:
        return WhiteboxTopologyClass::Parallel;
    case WhiteboxStageKind::Rigid:
        return WhiteboxTopologyClass::Rigid;
    case WhiteboxStageKind::Q:
        return WhiteboxTopologyClass::Unknown;
    }
    return WhiteboxTopologyClass::Unknown;
}

std::string StageId(const DecompositionNode& node) {
    const auto hash = [&node](std::uint64_t seed) {
        auto value = seed;
        for (const auto byte : node.key) {
            value ^= static_cast<unsigned char>(byte);
            value *= 1099511628211ULL;
        }
        return value;
    };
    const auto hex = [](std::uint64_t value) {
        constexpr char digits[] = "0123456789abcdef";
        std::string result(16U, '0');
        for (std::size_t index = 0; index < result.size(); ++index) {
            result[result.size() - index - 1U] = digits[value & 0x0fU];
            value >>= 4U;
        }
        return result;
    };
    const char kind = node.kind == WhiteboxStageKind::Q ? 'q' :
        node.kind == WhiteboxStageKind::Series ? 's' :
        node.kind == WhiteboxStageKind::Parallel ? 'p' : 'r';
    return "stage-" + std::string(1U, kind) + "-" +
        std::to_string(node.component_ids.size()) + "-" +
        hex(hash(14695981039346656037ULL)) + hex(hash(7809847782465536322ULL));
}

void AppendStages(const std::shared_ptr<DecompositionNode>& node,
                  WhiteboxExecutionPlan& plan) {
    if (node->kind != WhiteboxStageKind::Rigid) {
        for (const auto& child : node->children) {
            AppendStages(child, plan);
        }
    }
    WhiteboxStageIr stage;
    stage.id = StageId(*node);
    stage.kind = node->kind;
    stage.backend = node->kind == WhiteboxStageKind::Rigid
        ? WhiteboxRuntimeBackend::Mna
        : WhiteboxRuntimeBackend::Wdf;
    stage.component_ids = node->component_ids;
    if (node->kind != WhiteboxStageKind::Rigid) {
        for (const auto& child : node->children) {
            stage.dependency_stage_ids.push_back(StageId(*child));
        }
    }
    stage.input_nodes = {node->input_node};
    stage.output_nodes = {node->output_node};
    stage.status = WhiteboxPlanStatus::Ready;
    plan.signal_order.push_back(stage.id);
    plan.stages.push_back(std::move(stage));
}

WhiteboxStageRuntimeError MapRuntimeError(WhiteboxRuntimeError error) noexcept {
    if (error == WhiteboxRuntimeError::None) {
        return WhiteboxStageRuntimeError::None;
    }
    if (error == WhiteboxRuntimeError::NonFinite) {
        return WhiteboxStageRuntimeError::NonFiniteInput;
    }
    return WhiteboxStageRuntimeError::SolverFailure;
}

std::optional<WdfElementKind> WdfKind(std::string_view kind) {
    if (kind == "resistor") {
        return WdfElementKind::Resistor;
    }
    if (kind == "capacitor") {
        return WdfElementKind::Capacitor;
    }
    if (kind == "inductor") {
        return WdfElementKind::Inductor;
    }
    if (kind == "transformer") {
        return WdfElementKind::Transformer;
    }
    return std::nullopt;
}

bool ValidRuntimeOptions(const WhiteboxRuntimeOptions& options) {
    return (options.backend == WhiteboxRuntimeBackend::Mna ||
            options.backend == WhiteboxRuntimeBackend::Wdf) &&
        std::isfinite(options.sample_rate_hz) && options.sample_rate_hz > 0.0 &&
        options.oversampling > 0U && options.oversampling <= 16U &&
        options.max_iterations > 0U && options.max_iterations <= 1024U &&
        std::isfinite(options.absolute_tolerance) && options.absolute_tolerance > 0.0 &&
        std::isfinite(options.relative_tolerance) && options.relative_tolerance > 0.0 &&
        std::isfinite(options.maximum_initial_state) && options.maximum_initial_state > 0.0;
}

} // namespace

WhiteboxDecompositionResult AnalyzeWhiteboxGraph(const WhiteboxCompiledGraph& graph) {
    if (!graph.Subcircuits().empty()) {
        return Failure(WhiteboxDecompositionError::CrossNetworkBoundary, graph.Classification());
    }
    if (graph.Components().size() > kMaximumStageComponents) {
        return Failure(WhiteboxDecompositionError::UnsupportedTopology, graph.Classification());
    }
    if (graph.Components().empty() ||
        std::any_of(graph.Components().begin(), graph.Components().end(), [](const auto& component) {
            return component.pins.size() != 2U;
        })) {
        return Failure(WhiteboxDecompositionError::NonDecomposable, graph.Classification());
    }
    const auto terminals = NetworkTerminals(graph);
    if (!terminals.has_value()) {
        return Failure(WhiteboxDecompositionError::InvalidGraph, graph.Classification());
    }
    const auto root = ReduceGraph(graph, terminals->first, terminals->second);
    if (!root) {
        return Failure(WhiteboxDecompositionError::NonDecomposable, graph.Classification());
    }

    WhiteboxExecutionPlan plan;
    plan.classification = graph.Classification();
    plan.graph = graph;
    WhiteboxPartitionIr partition;
    partition.id = "partition-root";
    partition.classification = TopologyFor(root->kind);
    partition.component_ids = root->component_ids;
    partition.boundary_nodes = {terminals->first, terminals->second};
    SortUnique(partition.boundary_nodes);
    plan.partitions.push_back(std::move(partition));
    AppendStages(root, plan);
    plan.feedback = {.status = WhiteboxPlanStatus::Ready, .detected = false};
    plan.blockwise.status = WhiteboxPlanStatus::Ready;
    plan.blockwise.stage_ids = plan.signal_order;
    plan.blockwise.offsets.resize(plan.signal_order.size() + 1U);
    std::iota(plan.blockwise.offsets.begin(), plan.blockwise.offsets.end(), 0U);
    plan.k_method.status = WhiteboxPlanStatus::Planned;
    plan.k_method.coefficients.assign(plan.signal_order.size(), 1.0);
    plan.assembly.status = WhiteboxPlanStatus::Ready;
    plan.assembly.stage_ids = plan.signal_order;
    return {.plan = std::move(plan), .error = std::nullopt, .classification = graph.Classification()};
}

WhiteboxDecompositionResult AnalyzeWhiteboxGraphV2Json(std::string_view source) {
    const auto compiled = CompileWhiteboxGraphV2Json(source);
    if (!compiled.graph.has_value()) {
        return Failure(WhiteboxDecompositionError::InvalidGraph, compiled.classification);
    }
    return AnalyzeWhiteboxGraph(*compiled.graph);
}

void WhiteboxStageRuntime::RecomputeWdfResistances() noexcept {
    for (auto& stage : wdf_stages_) {
        if (stage.kind == WhiteboxStageKind::Q) {
            stage.resistance = stage.port.has_value() ? stage.port->PortResistance() : 0.0;
        } else if (stage.kind == WhiteboxStageKind::Series) {
            stage.resistance = 0.0;
            for (const auto child : stage.children) {
                stage.resistance += wdf_stages_[child].resistance;
            }
        } else if (stage.kind == WhiteboxStageKind::Parallel) {
            double conductance = 0.0;
            for (const auto child : stage.children) {
                conductance += 1.0 / wdf_stages_[child].resistance;
            }
            stage.resistance = conductance > 0.0 ? 1.0 / conductance : 0.0;
        }
    }
}

void WhiteboxStageRuntime::ScatterDown(std::size_t index, double incident) noexcept {
    auto& stage = wdf_stages_[index];
    stage.incident = incident;
    stage.voltage = (incident + stage.reflected) * 0.5;
    if (stage.kind == WhiteboxStageKind::Q) {
        double ignored{};
        static_cast<void>(stage.port->Process(incident, ignored));
        return;
    }
    if (stage.kind == WhiteboxStageKind::Series) {
        const double delta = incident - stage.reflected;
        for (const auto child : stage.children) {
            const auto child_incident = wdf_stages_[child].reflected +
                (wdf_stages_[child].resistance / stage.resistance) * delta;
            ScatterDown(child, child_incident);
        }
        return;
    }
    for (const auto child : stage.children) {
        ScatterDown(child, incident + stage.reflected - wdf_stages_[child].reflected);
    }
}

void WhiteboxStageRuntime::CaptureOutput(std::size_t index, double input_potential) noexcept {
    const auto& stage = wdf_stages_[index];
    if (stage.input_node == output_node_) {
        captured_output_ = input_potential;
        output_captured_ = true;
    }
    const double output_potential = input_potential - stage.voltage;
    if (stage.output_node == output_node_) {
        captured_output_ = output_potential;
        output_captured_ = true;
    }
    if (stage.kind == WhiteboxStageKind::Series) {
        double current = input_potential;
        for (const auto child : stage.children) {
            CaptureOutput(child, current);
            current -= wdf_stages_[child].voltage;
        }
    } else if (stage.kind == WhiteboxStageKind::Parallel) {
        for (const auto child : stage.children) {
            CaptureOutput(child, input_potential);
        }
    }
}

WhiteboxStageRuntimeError WhiteboxStageRuntime::Process(double input, double& output) noexcept {
    output = 0.0;
    if (!prepared_) {
        return WhiteboxStageRuntimeError::NotPrepared;
    }
    if (!std::isfinite(input)) {
        return WhiteboxStageRuntimeError::NonFiniteInput;
    }
    if (backend_ == WhiteboxRuntimeBackend::Mna) {
        return mna_runtime_.has_value()
            ? MapRuntimeError(mna_runtime_->Process(input, output))
            : WhiteboxStageRuntimeError::NotPrepared;
    }
    if (wdf_stages_.empty()) {
        return WhiteboxStageRuntimeError::NotPrepared;
    }
    for (auto& stage : wdf_stages_) {
        if (stage.kind == WhiteboxStageKind::Q) {
            stage.reflected = stage.port->ReflectedWave();
        } else if (stage.kind == WhiteboxStageKind::Series) {
            stage.reflected = 0.0;
            for (const auto child : stage.children) {
                stage.reflected += wdf_stages_[child].reflected;
            }
        } else if (stage.kind == WhiteboxStageKind::Parallel) {
            double weighted = 0.0;
            for (const auto child : stage.children) {
                weighted += wdf_stages_[child].reflected / wdf_stages_[child].resistance;
            }
            stage.reflected = stage.resistance * weighted;
        }
        if (!std::isfinite(stage.reflected)) {
            return WhiteboxStageRuntimeError::SolverFailure;
        }
    }
    const auto root = wdf_stages_.size() - 1U;
    ScatterDown(root, 2.0 * input - wdf_stages_[root].reflected);
    output_captured_ = false;
    captured_output_ = 0.0;
    CaptureOutput(root, input);
    if (!output_captured_ || !std::isfinite(captured_output_)) {
        return WhiteboxStageRuntimeError::SolverFailure;
    }
    output = captured_output_;
    return WhiteboxStageRuntimeError::None;
}

bool WhiteboxStageRuntime::SetSampleRate(double sample_rate_hz) noexcept {
    if (!prepared_ || !std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return false;
    }
    if (backend_ == WhiteboxRuntimeBackend::Mna) {
        return mna_runtime_.has_value() && mna_runtime_->SetSampleRate(sample_rate_hz);
    }
    for (const auto& stage : wdf_stages_) {
        if (stage.kind == WhiteboxStageKind::Q && !stage.port->CanSetSampleRate(sample_rate_hz)) {
            return false;
        }
    }
    for (auto& stage : wdf_stages_) {
        if (stage.kind == WhiteboxStageKind::Q && !stage.port->SetSampleRate(sample_rate_hz)) {
            return false;
        }
    }
    RecomputeWdfResistances();
    return true;
}

void WhiteboxStageRuntime::Reset() noexcept {
    if (backend_ == WhiteboxRuntimeBackend::Mna) {
        if (mna_runtime_.has_value()) {
            mna_runtime_->Reset();
        }
        return;
    }
    for (auto& stage : wdf_stages_) {
        if (stage.kind == WhiteboxStageKind::Q && stage.port.has_value()) {
            stage.port->Reset();
        }
        stage.incident = 0.0;
        stage.reflected = 0.0;
        stage.voltage = 0.0;
    }
}

WhiteboxStageRuntimePrepareResult PrepareWhiteboxStageRuntime(
    const WhiteboxExecutionPlan& plan,
    const WhiteboxRuntimeOptions& options) {
    if (plan.assembly.status != WhiteboxPlanStatus::Ready || !plan.graph.has_value() ||
        plan.stages.empty() || plan.signal_order.size() != plan.stages.size() ||
        plan.assembly.stage_ids != plan.signal_order || !ValidRuntimeOptions(options)) {
        return {.runtime = std::nullopt, .error = WhiteboxStageRuntimeError::UnsupportedAssembly};
    }
    const auto& graph = *plan.graph;
    std::vector<std::string> graph_component_ids;
    graph_component_ids.reserve(graph.Components().size());
    for (const auto& component : graph.Components()) {
        graph_component_ids.push_back(component.id);
    }
    SortUnique(graph_component_ids);
    for (std::size_t index = 0; index < plan.stages.size(); ++index) {
        if (plan.stages[index].id != plan.signal_order[index]) {
            return {.runtime = std::nullopt, .error = WhiteboxStageRuntimeError::UnsupportedAssembly};
        }
    }
    const auto& root = plan.stages.back();
    if (root.component_ids != graph_component_ids) {
        return {.runtime = std::nullopt, .error = WhiteboxStageRuntimeError::UnsupportedAssembly};
    }
    WhiteboxStageRuntime runtime;
    if (root.kind == WhiteboxStageKind::Rigid) {
        auto runtime_options = options;
        runtime_options.backend = WhiteboxRuntimeBackend::Mna;
        auto prepared = PrepareWhiteboxRuntime(graph, runtime_options);
        if (!prepared.runtime.has_value()) {
            return {.runtime = std::nullopt, .error = WhiteboxStageRuntimeError::UnsupportedAssembly};
        }
        runtime.backend_ = WhiteboxRuntimeBackend::Mna;
        runtime.mna_runtime_ = std::move(*prepared.runtime);
        runtime.prepared_ = true;
        return {.runtime = std::move(runtime), .error = std::nullopt};
    }

    if (!graph.Supplies().empty() || root.output_nodes.size() != 1U ||
        root.output_nodes[0] != graph.ReferenceGround() || options.oversampling != 1U) {
        return {.runtime = std::nullopt, .error = WhiteboxStageRuntimeError::UnsupportedAssembly};
    }
    const auto output = PortNode(graph, WhiteboxPortDirection::Output);
    if (!output.has_value()) {
        return {.runtime = std::nullopt, .error = WhiteboxStageRuntimeError::UnsupportedAssembly};
    }
    std::unordered_map<std::string, const WhiteboxComponent*> components;
    for (const auto& component : graph.Components()) {
        components.emplace(component.id, &component);
    }
    std::unordered_map<std::string, std::size_t> stage_indices;
    runtime.wdf_stages_.reserve(plan.stages.size());
    for (const auto& stage : plan.stages) {
        WhiteboxStageRuntime::WdfStage prepared_stage;
        prepared_stage.kind = stage.kind;
        if (stage.input_nodes.size() != 1U || stage.output_nodes.size() != 1U ||
            stage.kind == WhiteboxStageKind::Rigid) {
            return {.runtime = std::nullopt, .error = WhiteboxStageRuntimeError::UnsupportedAssembly};
        }
        prepared_stage.input_node = stage.input_nodes[0];
        prepared_stage.output_node = stage.output_nodes[0];
        for (const auto& dependency : stage.dependency_stage_ids) {
            const auto iterator = stage_indices.find(dependency);
            if (iterator == stage_indices.end()) {
                return {.runtime = std::nullopt, .error = WhiteboxStageRuntimeError::UnsupportedAssembly};
            }
            prepared_stage.children.push_back(iterator->second);
        }
        if (stage.kind == WhiteboxStageKind::Q) {
            if (stage.component_ids.size() != 1U) {
                return {.runtime = std::nullopt, .error = WhiteboxStageRuntimeError::UnsupportedAssembly};
            }
            const auto component_iterator = components.find(stage.component_ids[0]);
            if (component_iterator == components.end() || component_iterator->second->model_reference.has_value() ||
                !component_iterator->second->value.has_value()) {
                return {.runtime = std::nullopt, .error = WhiteboxStageRuntimeError::UnsupportedAssembly};
            }
            const auto kind = WdfKind(component_iterator->second->kind);
            const auto& pins = component_iterator->second->pins;
            const bool forward = pins.size() == 2U &&
                pins[0] == prepared_stage.input_node && pins[1] == prepared_stage.output_node;
            const bool reversed = pins.size() == 2U &&
                pins[1] == prepared_stage.input_node && pins[0] == prepared_stage.output_node;
            if (!forward && !reversed) {
                return {.runtime = std::nullopt, .error = WhiteboxStageRuntimeError::UnsupportedAssembly};
            }
            const auto initial = component_iterator->second->initial.value_or(0.0) *
                (forward ? 1.0 : -1.0);
            if (!kind.has_value() || !std::isfinite(initial) ||
                std::fabs(initial) > options.maximum_initial_state) {
                return {.runtime = std::nullopt, .error = WhiteboxStageRuntimeError::UnsupportedAssembly};
            }
            auto port = PrepareWdfOnePort(*kind, *component_iterator->second->value,
                                          options.sample_rate_hz, initial);
            if (!port.port.has_value()) {
                return {.runtime = std::nullopt, .error = WhiteboxStageRuntimeError::UnsupportedAssembly};
            }
            prepared_stage.port = std::move(*port.port);
        } else if (prepared_stage.children.size() < 2U) {
            return {.runtime = std::nullopt, .error = WhiteboxStageRuntimeError::UnsupportedAssembly};
        }
        const auto index = runtime.wdf_stages_.size();
        runtime.wdf_stages_.push_back(std::move(prepared_stage));
        if (!stage_indices.emplace(stage.id, index).second) {
            return {.runtime = std::nullopt, .error = WhiteboxStageRuntimeError::UnsupportedAssembly};
        }
    }
    runtime.RecomputeWdfResistances();
    if (std::any_of(runtime.wdf_stages_.begin(), runtime.wdf_stages_.end(), [](const auto& stage) {
            return stage.resistance <= 0.0 || !std::isfinite(stage.resistance);
        })) {
        return {.runtime = std::nullopt, .error = WhiteboxStageRuntimeError::UnsupportedAssembly};
    }
    runtime.output_node_ = *output;
    runtime.backend_ = WhiteboxRuntimeBackend::Wdf;
    runtime.prepared_ = true;
    runtime.Reset();
    return {.runtime = std::move(runtime), .error = std::nullopt};
}

} // namespace solo_rig
