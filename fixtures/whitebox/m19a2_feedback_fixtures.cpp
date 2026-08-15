#include "m19a2_feedback_fixtures.hpp"

#include <string>
#include <utility>

namespace solo_rig::m19a2_fixtures {

namespace {

BehaviorGraphNodeSpec Node(std::string id, std::string kind, std::string model) {
    return {.id = id, .binding = {.module_id = id, .kind = std::move(kind),
        .model_id = std::move(model)}};
}

} // namespace

FeedbackFixture VcoBbdSingleFeedback() {
    return {
        {Node("bbd", "bbd", "generic-bbd-v1"),
            Node("vco", "vco", "generic-vco-v1")},
        {{"input", "audio", "bbd", "audio_in"},
            {"vco", "out", "bbd", "clock"},
            {"bbd", "out", "bbd", "feedback"},
            {"bbd", "out", "output", "audio"}}};
}

} // namespace solo_rig::m19a2_fixtures
