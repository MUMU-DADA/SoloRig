#pragma once

#include "content/behavior_graph.hpp"
#include "content/behavior_ports.hpp"

#include <vector>

namespace solo_rig::m19a1_fixtures {

// Self-authored generic structure fixtures. They contain no external file paths
// and are only for validating the existing behavior Compile/runtime contracts.
struct TypedFixture {
    std::vector<BehaviorGraphNodeSpec> nodes;
    std::vector<BehaviorPortRouteSpec> routes;
};

struct SingleSignalFixture {
    std::vector<BehaviorGraphNodeSpec> nodes;
    std::vector<BehaviorGraphRouteSpec> routes;
};

[[nodiscard]] TypedFixture TremoloLfoToVca();
[[nodiscard]] TypedFixture EnvelopeVcaOpticalLike();
[[nodiscard]] TypedFixture EnvelopeDetectorOutput();
[[nodiscard]] TypedFixture OptocouplerResistance();
[[nodiscard]] SingleSignalFixture BbdVcoSpringSerial();

} // namespace solo_rig::m19a1_fixtures
