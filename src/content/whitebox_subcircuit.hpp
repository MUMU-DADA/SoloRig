#pragma once

#include "content/json.hpp"
#include "content/whitebox_compiler.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace solo_rig {

struct WhiteboxSubcircuitExpansion {
    std::vector<WhiteboxNode> nodes;
    std::vector<WhiteboxComponent> components;
    std::vector<WhiteboxSubcircuit> unresolved;
    std::optional<WhiteboxCompileError> error;
};

[[nodiscard]] WhiteboxSubcircuitExpansion ExpandWhiteboxSubcircuits(
    const JsonValue::Array& subcircuits,
    const std::unordered_set<std::string>& parent_nodes,
    std::unordered_set<std::string>& graph_ids,
    std::size_t existing_component_count);

} // namespace solo_rig
