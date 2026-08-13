#include "content/whitebox_compiler.hpp"

#include "content/json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace solo_rig {

namespace {

const JsonValue* Find(const JsonValue::Object& object, std::string_view key) {
    const auto iterator = object.find(key);
    return iterator == object.end() ? nullptr : &iterator->second;
}

std::optional<std::string> StringValue(const JsonValue* value) {
    if (value == nullptr || value->AsString() == nullptr || value->AsString()->empty()) {
        return std::nullopt;
    }
    return *value->AsString();
}

std::optional<double> NumberValue(const JsonValue* value) {
    if (value == nullptr || value->AsNumber() == nullptr || !std::isfinite(*value->AsNumber())) {
        return std::nullopt;
    }
    return *value->AsNumber();
}

bool IsSafeRelativePath(std::string_view path) {
    if (path.empty()) {
        return false;
    }
    const std::filesystem::path candidate{std::string(path)};
    if (candidate.is_absolute() || candidate.has_root_name() || candidate.has_root_directory()) {
        return false;
    }
    for (const auto& part : candidate) {
        if (part == "." || part == "..") {
            return false;
        }
    }
    return true;
}

WhiteboxCompileResult Failure(WhiteboxCompileError error,
                              WhiteboxTopologyClass classification = WhiteboxTopologyClass::Unknown) {
    return {.graph = std::nullopt, .error = error, .classification = classification};
}

bool HasNode(const std::unordered_set<std::string>& nodes, std::string_view node) {
    return nodes.find(std::string(node)) != nodes.end();
}

WhiteboxTopologyClass Classify(const WhiteboxCompiledGraph& graph) {
    if (!graph.Subcircuits().empty()) {
        return WhiteboxTopologyClass::Rigid;
    }
    if (graph.Components().empty()) {
        return WhiteboxTopologyClass::Unknown;
    }
    for (const auto& component : graph.Components()) {
        if (component.pins.size() != 2U) {
            return WhiteboxTopologyClass::Rigid;
        }
    }

    std::unordered_map<std::string, std::vector<std::string>> adjacency;
    std::unordered_map<std::string, std::size_t> degree;
    std::unordered_set<std::string> pair_keys;
    bool duplicate_pair = false;
    for (const auto& component : graph.Components()) {
        const auto& first = component.pins[0];
        const auto& second = component.pins[1];
        const auto pair_key = first < second ? first + "\n" + second : second + "\n" + first;
        if (!pair_keys.insert(pair_key).second) {
            duplicate_pair = true;
        }
        adjacency[first].push_back(second);
        adjacency[second].push_back(first);
        ++degree[first];
        ++degree[second];
    }
    if (duplicate_pair) {
        return WhiteboxTopologyClass::Parallel;
    }

    std::unordered_set<std::string> visited;
    std::vector<std::string> pending{adjacency.begin()->first};
    while (!pending.empty()) {
        auto node = std::move(pending.back());
        pending.pop_back();
        if (!visited.insert(node).second) {
            continue;
        }
        for (const auto& neighbour : adjacency[node]) {
            if (!visited.contains(neighbour)) {
                pending.push_back(neighbour);
            }
        }
    }
    if (visited.size() != adjacency.size()) {
        return WhiteboxTopologyClass::Unknown;
    }
    std::size_t endpoints{};
    for (const auto& [node, count] : degree) {
        static_cast<void>(node);
        if (count == 1U) {
            ++endpoints;
        } else if (count != 2U) {
            return WhiteboxTopologyClass::Spqr;
        }
    }
    return endpoints == 2U ? WhiteboxTopologyClass::Series : WhiteboxTopologyClass::Spqr;
}

} // namespace

WhiteboxProcessError WhiteboxPreparedGraph::Process(double input, double& output) noexcept {
    output = 0.0;
    if (!prepared_) {
        return WhiteboxProcessError::NotPrepared;
    }
    if (!std::isfinite(input)) {
        return WhiteboxProcessError::NonFiniteInput;
    }
    // M13 owns the actual WDF/MNA runtime. M12 deliberately exposes the
    // refusal instead of pretending that topology IR is an audio processor.
    return WhiteboxProcessError::UnsupportedRuntime;
}

WhiteboxPrepareResult WhiteboxCompiledGraph::Prepare() const {
    if (classification_ != WhiteboxTopologyClass::Series && classification_ != WhiteboxTopologyClass::Parallel) {
        return {.graph = std::nullopt, .error = WhiteboxCompileError::UnsupportedTopology};
    }
    WhiteboxPreparedGraph prepared;
    prepared.prepared_ = true;
    return {.graph = std::move(prepared), .error = std::nullopt};
}

WhiteboxCompileResult CompileWhiteboxGraphV2Json(std::string_view source) {
    const auto parsed = ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        return Failure(WhiteboxCompileError::InvalidJson);
    }
    const auto& object = *parsed.value->AsObject();
    const auto version = NumberValue(Find(object, "version"));
    if (!version.has_value() || *version != 2.0) {
        return Failure(WhiteboxCompileError::UnsupportedVersion);
    }
    const auto schema = StringValue(Find(object, "schema"));
    if (!schema.has_value() || *schema != "circuit") {
        return Failure(WhiteboxCompileError::InvalidSchema);
    }
    const auto ground = StringValue(Find(object, "referenceGround"));
    const auto* nodes_value = Find(object, "nodes");
    const auto* nodes_array = nodes_value == nullptr ? nullptr : nodes_value->AsArray();
    if (!ground.has_value() || nodes_array == nullptr || nodes_array->empty()) {
        return Failure(WhiteboxCompileError::MissingField);
    }

    WhiteboxCompiledGraph graph;
    std::unordered_set<std::string> node_ids;
    for (const auto& node_value : *nodes_array) {
        const auto node = StringValue(&node_value);
        if (!node.has_value() || !node_ids.insert(*node).second) {
            return Failure(WhiteboxCompileError::DuplicateId);
        }
        graph.nodes_.push_back({.id = *node});
    }
    if (!node_ids.contains(*ground)) {
        return Failure(WhiteboxCompileError::InvalidNode);
    }

    std::unordered_set<std::string> ids;
    const auto* ports_array = Find(object, "ports") == nullptr ? nullptr : Find(object, "ports")->AsArray();
    if (ports_array == nullptr || ports_array->empty()) {
        return Failure(WhiteboxCompileError::MissingField);
    }
    bool has_input = false;
    bool has_output = false;
    for (const auto& port_value : *ports_array) {
        const auto* port = port_value.AsObject();
        const auto id = port == nullptr ? std::nullopt : StringValue(Find(*port, "id"));
        const auto direction = port == nullptr ? std::nullopt : StringValue(Find(*port, "direction"));
        const auto node = port == nullptr ? std::nullopt : StringValue(Find(*port, "node"));
        if (!id.has_value() || !direction.has_value() || !node.has_value()) {
            return Failure(WhiteboxCompileError::InvalidPort);
        }
        if (!ids.insert(*id).second) {
            return Failure(WhiteboxCompileError::DuplicateId);
        }
        if (!HasNode(node_ids, *node)) {
            return Failure(WhiteboxCompileError::InvalidPort);
        }
        WhiteboxPortDirection parsed_direction;
        if (*direction == "input") {
            parsed_direction = WhiteboxPortDirection::Input;
            has_input = true;
        } else if (*direction == "output") {
            parsed_direction = WhiteboxPortDirection::Output;
            has_output = true;
        } else {
            return Failure(WhiteboxCompileError::InvalidPort);
        }
        graph.ports_.push_back({.id = *id, .direction = parsed_direction, .node = *node});
    }
    if (!has_input || !has_output) {
        return Failure(WhiteboxCompileError::InvalidPort);
    }

    if (const auto* supplies_value = Find(object, "supplies"); supplies_value != nullptr) {
        const auto* supplies = supplies_value->AsArray();
        if (supplies == nullptr) {
            return Failure(WhiteboxCompileError::InvalidSupply);
        }
        for (const auto& supply_value : *supplies) {
            const auto* supply = supply_value.AsObject();
            const auto id = supply == nullptr ? std::nullopt : StringValue(Find(*supply, "id"));
            const auto positive = supply == nullptr ? std::nullopt : StringValue(Find(*supply, "positive"));
            const auto negative = supply == nullptr ? std::nullopt : StringValue(Find(*supply, "negative"));
            const auto voltage = supply == nullptr ? std::nullopt : NumberValue(Find(*supply, "voltage"));
            if (!id.has_value() || !positive.has_value() || !negative.has_value() || !voltage.has_value()) {
                return Failure(WhiteboxCompileError::MissingField);
            }
            if (!ids.insert(*id).second) {
                return Failure(WhiteboxCompileError::DuplicateId);
            }
            if (!HasNode(node_ids, *positive) || !HasNode(node_ids, *negative) || *positive == *negative) {
                return Failure(WhiteboxCompileError::InvalidSupply);
            }
            graph.supplies_.push_back({.id = *id, .positive = *positive, .negative = *negative, .voltage = *voltage});
        }
    }

    const auto* components_value = Find(object, "components");
    const auto* components = components_value == nullptr ? nullptr : components_value->AsArray();
    if (components == nullptr) {
        return Failure(WhiteboxCompileError::MissingField);
    }
    for (const auto& component_value : *components) {
        const auto* component = component_value.AsObject();
        const auto id = component == nullptr ? std::nullopt : StringValue(Find(*component, "id"));
        const auto kind = component == nullptr ? std::nullopt : StringValue(Find(*component, "kind"));
        const auto* pins_value = component == nullptr ? nullptr : Find(*component, "pins");
        const auto* pins = pins_value == nullptr ? nullptr : pins_value->AsArray();
        if (!id.has_value() || !kind.has_value()) {
            return Failure(WhiteboxCompileError::MissingField);
        }
        if (!ids.insert(*id).second) {
            return Failure(WhiteboxCompileError::DuplicateId);
        }
        if (pins == nullptr || pins->size() < 2U) {
            return Failure(WhiteboxCompileError::InvalidPin);
        }
        WhiteboxComponent parsed_component{.id = *id, .kind = *kind};
        for (const auto& pin_value : *pins) {
            const auto pin = StringValue(&pin_value);
            if (!pin.has_value() || !HasNode(node_ids, *pin)) {
                return Failure(WhiteboxCompileError::InvalidPin);
            }
            parsed_component.pins.push_back(*pin);
        }
        if (const auto model = StringValue(Find(*component, "modelReference")); model.has_value()) {
            parsed_component.model_reference = *model;
        }
        graph.components_.push_back(std::move(parsed_component));
    }

    if (const auto* subcircuits_value = Find(object, "subcircuits"); subcircuits_value != nullptr) {
        const auto* subcircuits = subcircuits_value->AsArray();
        if (subcircuits == nullptr) {
            return Failure(WhiteboxCompileError::InvalidSubcircuit);
        }
        for (const auto& subcircuit_value : *subcircuits) {
            const auto* subcircuit = subcircuit_value.AsObject();
            const auto id = subcircuit == nullptr ? std::nullopt : StringValue(Find(*subcircuit, "id"));
            const auto* ports_value = subcircuit == nullptr ? nullptr : Find(*subcircuit, "ports");
            const auto* ports = ports_value == nullptr ? nullptr : ports_value->AsArray();
            if (!id.has_value()) {
                return Failure(WhiteboxCompileError::MissingField);
            }
            if (!ids.insert(*id).second) {
                return Failure(WhiteboxCompileError::DuplicateId);
            }
            if (ports == nullptr || ports->size() < 2U) {
                return Failure(WhiteboxCompileError::InvalidSubcircuit);
            }
            WhiteboxSubcircuit parsed_subcircuit{.id = *id};
            for (const auto& port_value : *ports) {
                const auto port = StringValue(&port_value);
                if (!port.has_value() || !HasNode(node_ids, *port)) {
                    return Failure(WhiteboxCompileError::InvalidSubcircuit);
                }
                parsed_subcircuit.ports.push_back(*port);
            }
            if (const auto model = StringValue(Find(*subcircuit, "modelReference")); model.has_value()) {
                parsed_subcircuit.model_reference = *model;
            }
            graph.subcircuits_.push_back(std::move(parsed_subcircuit));
        }
    }

    if (const auto* models_value = Find(object, "modelReferences"); models_value != nullptr) {
        const auto* models = models_value->AsArray();
        if (models == nullptr) {
            return Failure(WhiteboxCompileError::InvalidModelReference);
        }
        std::unordered_set<std::string> model_ids;
        for (const auto& model_value : *models) {
            const auto* model = model_value.AsObject();
            const auto id = model == nullptr ? std::nullopt : StringValue(Find(*model, "id"));
            const auto path = model == nullptr ? std::nullopt : StringValue(Find(*model, "path"));
            const auto status = model == nullptr ? std::nullopt : StringValue(Find(*model, "status"));
            if (!id.has_value() || !path.has_value() || !status.has_value() || !model_ids.insert(*id).second) {
                return Failure(WhiteboxCompileError::InvalidModelReference);
            }
            if (!IsSafeRelativePath(*path)) {
                return Failure(WhiteboxCompileError::UnsafeModelPath);
            }
            graph.model_references_.push_back({.id = *id, .path = *path, .status = *status});
        }
    }
    std::unordered_set<std::string> model_ids;
    for (const auto& model : graph.model_references_) {
        model_ids.insert(model.id);
    }
    for (const auto& component : graph.components_) {
        if (component.model_reference.has_value() && !model_ids.contains(*component.model_reference)) {
            return Failure(WhiteboxCompileError::MissingModelReference);
        }
    }
    for (const auto& subcircuit : graph.subcircuits_) {
        if (subcircuit.model_reference.has_value() && !model_ids.contains(*subcircuit.model_reference)) {
            return Failure(WhiteboxCompileError::MissingModelReference);
        }
    }

    graph.backend_ = WhiteboxBackend::Mna;
    if (const auto* backend_value = Find(object, "backend"); backend_value != nullptr) {
        const auto* backend = backend_value->AsObject();
        const auto preferred = backend == nullptr ? std::nullopt : StringValue(Find(*backend, "preferred"));
        if (!preferred.has_value()) {
            return Failure(WhiteboxCompileError::InvalidBackend);
        }
        if (*preferred == "mna") {
            graph.backend_ = WhiteboxBackend::Mna;
        } else if (*preferred == "wdf") {
            graph.backend_ = WhiteboxBackend::Wdf;
        } else {
            return Failure(WhiteboxCompileError::InvalidBackend);
        }
    }

    graph.classification_ = Classify(graph);
    if (graph.classification_ == WhiteboxTopologyClass::Unknown) {
        return Failure(WhiteboxCompileError::InvalidTopology, graph.classification_);
    }
    if (graph.classification_ == WhiteboxTopologyClass::Spqr || graph.classification_ == WhiteboxTopologyClass::Rigid) {
        return Failure(WhiteboxCompileError::UnsupportedTopology, graph.classification_);
    }
    return {.graph = std::move(graph), .error = std::nullopt, .classification = graph.classification_};
}

} // namespace solo_rig
