#pragma once

#include "content/behavior_feedback.hpp"

#include <vector>

namespace solo_rig::m19a2_fixtures {

// Self-authored generic feedback structure; no external file or hardware model.
struct FeedbackFixture {
    std::vector<BehaviorGraphNodeSpec> nodes;
    std::vector<BehaviorPortRouteSpec> routes;
};

[[nodiscard]] FeedbackFixture VcoBbdSingleFeedback();

} // namespace solo_rig::m19a2_fixtures
