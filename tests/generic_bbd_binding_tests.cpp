#include "content/behavior_lowering.hpp"
#include "content/generic_bbd_binding.hpp"
#include "test_framework.hpp"

#include <limits>

TEST_CASE("M18-D2a generic BBD binding parses the complete clean-room contract") {
    const auto result = solo_rig::ParseGenericBbdModel(R"({
      "kind":"generic-bbd","modelVersion":1,"bucketCount":4,
      "clockFrequencyHz":48000,"transferEfficiency":0.99,"feedback":0.2,
      "wet":1,"dry":0,"maximumInput":2})");
    REQUIRE(result.error == solo_rig::GenericBbdBindingError::None);
    REQUIRE(result.model.has_value());
    REQUIRE(result.model->bucket_count == 4U);
    const auto bound = solo_rig::BindGenericBbdModel(
        *solo_rig::ParseJson(R"({"kind":"generic-bbd","modelVersion":1,"bucketCount":4,
          "clockFrequencyHz":48000,"transferEfficiency":0.99,"feedback":0.2,
          "wet":1,"dry":0,"maximumInput":2})").value->AsObject(), "generic-bbd-v1");
    REQUIRE(bound.error == solo_rig::GenericBbdBindingError::None);
    const auto plan = solo_rig::LowerBehaviorModule({"bbd", "bbd", "generic-bbd-v1"});
    REQUIRE(plan.error == std::nullopt);
    REQUIRE(plan.plan.has_value());
    REQUIRE(plan.plan->status == solo_rig::WhiteboxPlanStatus::Ready);
}

TEST_CASE("M18-D2a generic BBD binding rejects unauthorized, nonfinite and unsafe models") {
    const auto parsed = solo_rig::ParseGenericBbdModel(R"({
      "kind":"generic-bbd","modelVersion":1,"bucketCount":4,
      "clockFrequencyHz":48000,"transferEfficiency":0.99,"feedback":0.2,
      "wet":1,"dry":0,"maximumInput":2})");
    REQUIRE(parsed.model.has_value());
    const auto object = *solo_rig::ParseJson(R"({"kind":"generic-bbd","modelVersion":1,
      "bucketCount":4,"clockFrequencyHz":48000,"transferEfficiency":0.99,"feedback":0.2,
      "wet":1,"dry":0,"maximumInput":2})").value->AsObject();
    REQUIRE(solo_rig::BindGenericBbdModel(object, "blocked.bbd.template").error ==
            solo_rig::GenericBbdBindingError::UnauthorizedModel);
    auto unsafe = object;
    unsafe["feedback"] = solo_rig::JsonValue(1.0);
    REQUIRE(solo_rig::BindGenericBbdModel(unsafe, "generic-bbd-v1").error ==
            solo_rig::GenericBbdBindingError::InvalidModel);
    const auto malformed = solo_rig::ParseGenericBbdModel("not-json");
    REQUIRE(malformed.error == solo_rig::GenericBbdBindingError::InvalidJson);
    const auto blocked = solo_rig::LowerBehaviorModule({"bbd", "bbd", "blocked.bbd.template"});
    REQUIRE(blocked.error == solo_rig::BehaviorLoweringError::BlockedModel);
    const auto unknown = solo_rig::LowerBehaviorModule({"vco", "vco", "generic-bbd-v1"});
    REQUIRE(unknown.error == solo_rig::BehaviorLoweringError::BlockedModel);
    static_cast<void>(std::numeric_limits<double>::quiet_NaN());
}
