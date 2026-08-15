#include "content/behavior_lowering.hpp"
#include "content/generic_envelope_binding.hpp"
#include "test_framework.hpp"

namespace {

constexpr const char* kModel = R"({
  "kind":"generic-envelope","modelVersion":1,"inputGain":2,
  "attackSeconds":0.005,"releaseSeconds":0.05,"detectorCeiling":2,
  "outputScale":1,"maximumInput":2,"maximumOutput":2,"initialEnvelope":0.1})";

} // namespace

TEST_CASE("M18-D2c1 generic envelope binding parses and lowers the authorized model") {
    const auto parsed = solo_rig::ParseGenericEnvelopeModel(kModel);
    REQUIRE(parsed.error == solo_rig::GenericEnvelopeBindingError::None);
    REQUIRE(parsed.model.has_value());
    REQUIRE(parsed.model->attack_seconds == 0.005);

    const auto object = *solo_rig::ParseJson(kModel).value->AsObject();
    REQUIRE(solo_rig::BindGenericEnvelopeModel(object, "generic-envelope-v1").error ==
            solo_rig::GenericEnvelopeBindingError::None);
    const auto plan = solo_rig::LowerBehaviorModule(
        {"detector", "envelope", "generic-envelope-v1"});
    REQUIRE(plan.error == std::nullopt);
    REQUIRE(plan.plan.has_value());
    REQUIRE(plan.plan->kind == solo_rig::BehaviorKind::Envelope);
    REQUIRE(plan.plan->status == solo_rig::WhiteboxPlanStatus::Ready);
}

TEST_CASE("M18-D2c1 generic envelope binding rejects incomplete and unauthorized models") {
    const auto object = *solo_rig::ParseJson(kModel).value->AsObject();
    REQUIRE(solo_rig::BindGenericEnvelopeModel(object, "blocked.envelope.template").error ==
            solo_rig::GenericEnvelopeBindingError::UnauthorizedModel);
    auto invalid = object;
    invalid.erase("releaseSeconds");
    REQUIRE(solo_rig::BindGenericEnvelopeModel(invalid, "generic-envelope-v1").error ==
            solo_rig::GenericEnvelopeBindingError::InvalidModel);
    invalid = object;
    invalid["initialEnvelope"] = solo_rig::JsonValue(3.0);
    REQUIRE(solo_rig::BindGenericEnvelopeModel(invalid, "generic-envelope-v1").error ==
            solo_rig::GenericEnvelopeBindingError::InvalidModel);
    REQUIRE(solo_rig::ParseGenericEnvelopeModel("not-json").error ==
            solo_rig::GenericEnvelopeBindingError::InvalidJson);
    REQUIRE(solo_rig::LowerBehaviorModule(
        {"old-envelope", "envelope", "blocked.envelope.template"}).error ==
        solo_rig::BehaviorLoweringError::BlockedModel);
}
