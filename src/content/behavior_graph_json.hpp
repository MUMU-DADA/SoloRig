#pragma once

#include "content/behavior_ports.hpp"

#include <optional>
#include <string_view>

namespace solo_rig {

enum class BehaviorGraphJsonError {
    None,
    InvalidJson,
    UnsupportedVersion,
    InvalidSchema,
    MissingField,
    InvalidNode,
    InvalidRoute,
    GraphCompileFailure,
};

struct BehaviorGraphJsonCompileResult final {
    std::optional<BehaviorTypedGraphPlan> plan;
    BehaviorGraphJsonError error{BehaviorGraphJsonError::None};
    BehaviorPortCompileError graph_error{BehaviorPortCompileError::None};
};

// Parses only the non-real-time behavior graph document. The returned plan is
// fully validated by CompileTypedBehaviorGraph; no JSON or string lookup is
// required after this boundary.
[[nodiscard]] BehaviorGraphJsonCompileResult CompileTypedBehaviorGraphJson(
    std::string_view source);

} // namespace solo_rig
