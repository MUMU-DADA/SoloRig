#pragma once

#include "content/behavior_ports.hpp"

namespace solo_rig {

// Compile-only contract for the smallest legal envelope -> VCA composite.
[[nodiscard]] BehaviorPortCompileResult CompileEnvelopeVcaComposite(
    const std::vector<BehaviorGraphNodeSpec>& nodes,
    const std::vector<BehaviorPortRouteSpec>& routes);

} // namespace solo_rig
