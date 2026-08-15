#include "content/behavior_graph_json.hpp"

#include "content/json.hpp"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace solo_rig {

namespace {

const JsonValue* Find(const JsonValue::Object& object, std::string_view key) {
    const auto iterator = object.find(key);
    return iterator == object.end() ? nullptr : &iterator->second;
}

std::optional<double> NumberValue(const JsonValue* value) {
    if (value == nullptr || value->AsNumber() == nullptr || !std::isfinite(*value->AsNumber())) {
        return std::nullopt;
    }
    return *value->AsNumber();
}

std::optional<std::string> StringValue(const JsonValue* value) {
    if (value == nullptr || value->AsString() == nullptr || value->AsString()->empty()) {
        return std::nullopt;
    }
    return *value->AsString();
}

BehaviorGraphJsonCompileResult Failure(BehaviorGraphJsonError error) {
    return {.plan = std::nullopt, .error = error};
}

} // namespace

BehaviorGraphJsonCompileResult CompileTypedBehaviorGraphJson(std::string_view source) {
    const auto parsed = ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        return Failure(BehaviorGraphJsonError::InvalidJson);
    }
    const auto& object = *parsed.value->AsObject();
    const auto version = NumberValue(Find(object, "version"));
    if (!version.has_value() || *version != 1.0) {
        return Failure(BehaviorGraphJsonError::UnsupportedVersion);
    }
    const auto schema = StringValue(Find(object, "schema"));
    if (!schema.has_value() || *schema != "solorig-behavior-graph") {
        return Failure(BehaviorGraphJsonError::InvalidSchema);
    }

    const auto* nodes_value = Find(object, "nodes");
    const auto* nodes_array = nodes_value == nullptr ? nullptr : nodes_value->AsArray();
    const auto* routes_value = Find(object, "routes");
    const auto* routes_array = routes_value == nullptr ? nullptr : routes_value->AsArray();
    if (nodes_array == nullptr || nodes_array->empty() || routes_array == nullptr) {
        return Failure(BehaviorGraphJsonError::MissingField);
    }

    std::vector<BehaviorGraphNodeSpec> nodes;
    nodes.reserve(nodes_array->size());
    for (const auto& node_value : *nodes_array) {
        const auto* node = node_value.AsObject();
        const auto id = node == nullptr ? std::nullopt : StringValue(Find(*node, "id"));
        const auto module_id = node == nullptr ? std::nullopt : StringValue(Find(*node, "moduleId"));
        const auto kind = node == nullptr ? std::nullopt : StringValue(Find(*node, "kind"));
        const auto model_id = node == nullptr ? std::nullopt : StringValue(Find(*node, "modelId"));
        if (!id.has_value() || !module_id.has_value() || !kind.has_value() || !model_id.has_value()) {
            return Failure(BehaviorGraphJsonError::InvalidNode);
        }
        nodes.push_back({.id = *id,
                         .binding = {.module_id = *module_id, .kind = *kind, .model_id = *model_id}});
    }

    std::vector<BehaviorPortRouteSpec> routes;
    routes.reserve(routes_array->size());
    for (const auto& route_value : *routes_array) {
        const auto* route = route_value.AsObject();
        const auto source_name = route == nullptr ? std::nullopt : StringValue(Find(*route, "source"));
        const auto source_port = route == nullptr ? std::nullopt : StringValue(Find(*route, "sourcePort"));
        const auto destination = route == nullptr ? std::nullopt : StringValue(Find(*route, "destination"));
        const auto destination_port = route == nullptr
            ? std::nullopt : StringValue(Find(*route, "destinationPort"));
        if (!source_name.has_value() || !source_port.has_value() || !destination.has_value() ||
            !destination_port.has_value()) {
            return Failure(BehaviorGraphJsonError::InvalidRoute);
        }
        routes.push_back({.source = *source_name,
                          .source_port = *source_port,
                          .destination = *destination,
                          .destination_port = *destination_port});
    }

    const auto compiled = CompileTypedBehaviorGraph(nodes, routes);
    if (!compiled.plan.has_value() || compiled.error != BehaviorPortCompileError::None) {
        return {.plan = std::nullopt,
                .error = BehaviorGraphJsonError::GraphCompileFailure,
                .graph_error = compiled.error};
    }
    return {.plan = std::move(compiled.plan),
            .error = BehaviorGraphJsonError::None,
            .graph_error = BehaviorPortCompileError::None};
}

} // namespace solo_rig
