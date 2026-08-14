#include "content/whitebox_subcircuit.hpp"

#include <cmath>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace solo_rig {

namespace {

constexpr std::size_t kMaximumExpandedComponents = 256U;
constexpr std::size_t kMaximumInlineNodes = 256U;
constexpr std::size_t kMaximumInlineComponents = 256U;

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

WhiteboxSubcircuitExpansion Failure() {
    return {.error = WhiteboxCompileError::InvalidSubcircuit};
}

bool IsPassiveKind(std::string_view kind) {
    return kind == "resistor" || kind == "capacitor" || kind == "inductor";
}

} // namespace

WhiteboxSubcircuitExpansion ExpandWhiteboxSubcircuits(
    const JsonValue::Array& subcircuits,
    const std::unordered_set<std::string>& parent_nodes,
    std::unordered_set<std::string>& graph_ids,
    std::size_t existing_component_count) {
    WhiteboxSubcircuitExpansion result;
    auto generated_node_ids = parent_nodes;
    std::size_t expanded_component_count = existing_component_count;

    for (const auto& subcircuit_value : subcircuits) {
        const auto* subcircuit = subcircuit_value.AsObject();
        const auto id = subcircuit == nullptr ? std::nullopt : StringValue(Find(*subcircuit, "id"));
        const auto* ports_value = subcircuit == nullptr ? nullptr : Find(*subcircuit, "ports");
        const auto* ports = ports_value == nullptr ? nullptr : ports_value->AsArray();
        if (!id.has_value() || !graph_ids.insert(*id).second || ports == nullptr || ports->size() < 2U) {
            return Failure();
        }

        const auto* inline_value = Find(*subcircuit, "inline");
        const auto* port_map_value = Find(*subcircuit, "portMap");
        if (inline_value == nullptr) {
            if (port_map_value != nullptr) {
                return Failure();
            }
            WhiteboxSubcircuit unresolved{.id = *id};
            std::unordered_set<std::string> legacy_ports;
            for (const auto& port_value : *ports) {
                const auto port = StringValue(&port_value);
                if (!port.has_value() || !parent_nodes.contains(*port) || !legacy_ports.insert(*port).second) {
                    return Failure();
                }
                unresolved.ports.push_back(*port);
            }
            if (const auto model = StringValue(Find(*subcircuit, "modelReference")); model.has_value()) {
                unresolved.model_reference = *model;
            } else if (Find(*subcircuit, "modelReference") != nullptr) {
                return Failure();
            }
            result.unresolved.push_back(std::move(unresolved));
            continue;
        }

        if (Find(*subcircuit, "modelReference") != nullptr) {
            return Failure();
        }
        const auto* definition = inline_value->AsObject();
        const auto* port_map = port_map_value == nullptr ? nullptr : port_map_value->AsObject();
        const auto* nodes_value = definition == nullptr ? nullptr : Find(*definition, "nodes");
        const auto* local_nodes = nodes_value == nullptr ? nullptr : nodes_value->AsArray();
        const auto* components_value = definition == nullptr ? nullptr : Find(*definition, "components");
        const auto* local_components = components_value == nullptr ? nullptr : components_value->AsArray();
        if (definition == nullptr || port_map == nullptr || local_nodes == nullptr || local_nodes->empty() ||
            local_nodes->size() > kMaximumInlineNodes || local_components == nullptr || local_components->empty() ||
            local_components->size() > kMaximumInlineComponents || Find(*definition, "subcircuits") != nullptr) {
            return Failure();
        }

        std::vector<std::string> local_port_order;
        std::unordered_set<std::string> local_port_ids;
        local_port_order.reserve(ports->size());
        for (const auto& port_value : *ports) {
            const auto port = StringValue(&port_value);
            if (!port.has_value() || !local_port_ids.insert(*port).second) {
                return Failure();
            }
            local_port_order.push_back(*port);
        }
        if (port_map->size() != local_port_order.size()) {
            return Failure();
        }
        std::unordered_map<std::string, std::string> mapped_ports;
        std::unordered_set<std::string> mapped_parent_nodes;
        for (const auto& local_port : local_port_order) {
            const auto parent_node = StringValue(Find(*port_map, local_port));
            if (!parent_node.has_value() || !parent_nodes.contains(*parent_node) ||
                !mapped_parent_nodes.insert(*parent_node).second) {
                return Failure();
            }
            mapped_ports.emplace(local_port, *parent_node);
        }
        for (const auto& [mapped_name, mapped_value] : *port_map) {
            static_cast<void>(mapped_value);
            if (!local_port_ids.contains(mapped_name)) {
                return Failure();
            }
        }

        std::unordered_set<std::string> local_node_ids;
        std::unordered_map<std::string, std::string> flattened_nodes;
        for (const auto& node_value : *local_nodes) {
            const auto local_node = StringValue(&node_value);
            if (!local_node.has_value() || !local_node_ids.insert(*local_node).second) {
                return Failure();
            }
            const auto mapped = mapped_ports.find(*local_node);
            if (mapped != mapped_ports.end()) {
                flattened_nodes.emplace(*local_node, mapped->second);
                continue;
            }
            const auto generated = *id + "::" + *local_node;
            if (!generated_node_ids.insert(generated).second) {
                return Failure();
            }
            flattened_nodes.emplace(*local_node, generated);
            result.nodes.push_back({.id = generated});
        }
        for (const auto& local_port : local_port_order) {
            if (!local_node_ids.contains(local_port)) {
                return Failure();
            }
        }

        std::unordered_set<std::string> local_component_ids;
        for (const auto& component_value : *local_components) {
            const auto* component = component_value.AsObject();
            const auto component_id = component == nullptr ? std::nullopt : StringValue(Find(*component, "id"));
            const auto kind = component == nullptr ? std::nullopt : StringValue(Find(*component, "kind"));
            const auto* pins_value = component == nullptr ? nullptr : Find(*component, "pins");
            const auto* pins = pins_value == nullptr ? nullptr : pins_value->AsArray();
            const auto value = component == nullptr ? std::nullopt : NumberValue(Find(*component, "value"));
            if (!component_id.has_value() || !kind.has_value() || !IsPassiveKind(*kind) ||
                !local_component_ids.insert(*component_id).second || pins == nullptr || pins->size() != 2U ||
                !value.has_value() || *value <= 0.0 || Find(*component, "modelReference") != nullptr ||
                Find(*component, "nonlinearGroup") != nullptr) {
                return Failure();
            }
            const auto first_pin = StringValue(&(*pins)[0]);
            const auto second_pin = StringValue(&(*pins)[1]);
            if (!first_pin.has_value() || !second_pin.has_value() || *first_pin == *second_pin ||
                !local_node_ids.contains(*first_pin) || !local_node_ids.contains(*second_pin)) {
                return Failure();
            }
            const auto generated_id = *id + "::" + *component_id;
            if (!graph_ids.insert(generated_id).second) {
                return Failure();
            }
            WhiteboxComponent flattened{
                .id = generated_id,
                .kind = *kind,
                .pins = {flattened_nodes.at(*first_pin), flattened_nodes.at(*second_pin)},
                .value = *value};
            if (const auto initial = NumberValue(Find(*component, "initial")); initial.has_value()) {
                if (*kind == "resistor") {
                    return Failure();
                }
                flattened.initial = *initial;
            } else if (Find(*component, "initial") != nullptr) {
                return Failure();
            }
            result.components.push_back(std::move(flattened));
            ++expanded_component_count;
            if (expanded_component_count > kMaximumExpandedComponents) {
                return Failure();
            }
        }
    }
    return result;
}

} // namespace solo_rig
