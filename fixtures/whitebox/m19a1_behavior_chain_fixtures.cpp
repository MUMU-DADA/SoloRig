#include "m19a1_behavior_chain_fixtures.hpp"

#include <string>
#include <utility>

namespace solo_rig::m19a1_fixtures {

namespace {

BehaviorGraphNodeSpec Node(std::string id, std::string kind, std::string model) {
    return {.id = id, .binding = {.module_id = id, .kind = std::move(kind),
        .model_id = std::move(model)}};
}

} // namespace

TypedFixture TremoloLfoToVca() {
    return {
        {Node("vca", "vca", "generic-vca-v1"), Node("lfo", "lfo", "generic-lfo-v1")},
        {{"input", "audio", "vca", "audio_in"},
            {"lfo", "out", "vca", "control"}, {"vca", "out", "output", "audio"}}};
}

TypedFixture EnvelopeVcaOpticalLike() {
    return {
        {Node("vca", "vca", "generic-vca-v1"),
            Node("envelope", "envelope", "generic-envelope-v1")},
        {{"input", "audio", "vca", "audio_in"},
            {"input", "detector", "envelope", "detector"},
            {"envelope", "out", "vca", "control"},
            {"vca", "out", "output", "audio"}}};
}

TypedFixture EnvelopeDetectorOutput() {
    return {
        {Node("envelope", "envelope", "generic-envelope-v1")},
        {{"input", "audio", "envelope", "detector"},
            {"envelope", "out", "output", "control"}}};
}

TypedFixture OptocouplerResistance() {
    return {
        {Node("opto", "optocoupler", "generic-optocoupler-v1")},
        {{"input", "emitter", "opto", "emitter"},
            {"opto", "out", "output", "resistance"}}};
}

SingleSignalFixture BbdVcoSpringSerial() {
    return {
        {Node("bbd", "bbd", "generic-bbd-v1"), Node("vco", "vco", "generic-vco-v1"),
            Node("spring", "spring", "generic-spring-v1")},
        {{"input", "bbd"}, {"bbd", "vco"}, {"vco", "spring"}, {"spring", "output"}}};
}

} // namespace solo_rig::m19a1_fixtures
