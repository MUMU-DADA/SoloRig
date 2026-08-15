#include "content/behavior_lowering.hpp"
#include "content/generic_spring_binding.hpp"
#include "test_framework.hpp"

namespace {

constexpr const char* kModel = R"({
  "kind":"generic-spring","modelVersion":1,"modeCount":8,
  "baseFrequencyHz":90,"modeSpacingHz":73,"dispersion":0.35,
  "decaySeconds":1.8,"highFrequencyDamping":4,
  "driveGain":0.8,"pickupGain":1.2,"wet":0.7,"dry":0.3,
  "maximumInput":2,"maximumOutput":4})";

} // namespace

TEST_CASE("M18-D2d2 generic spring binding parses and lowers the authorized model") {
    const auto parsed = solo_rig::ParseGenericSpringModel(kModel);
    REQUIRE(parsed.error == solo_rig::GenericSpringBindingError::None);
    REQUIRE(parsed.model.has_value());
    REQUIRE(parsed.model->mode_count == 8U);
    REQUIRE(parsed.model->dispersion == 0.35);

    const auto object = *solo_rig::ParseJson(kModel).value->AsObject();
    REQUIRE(solo_rig::BindGenericSpringModel(object, "generic-spring-v1").error ==
            solo_rig::GenericSpringBindingError::None);
    const auto plan = solo_rig::LowerBehaviorModule(
        {"spring-tank", "spring", "generic-spring-v1"});
    REQUIRE(plan.error == std::nullopt);
    REQUIRE(plan.plan.has_value());
    REQUIRE(plan.plan->kind == solo_rig::BehaviorKind::Spring);
    REQUIRE(plan.plan->status == solo_rig::WhiteboxPlanStatus::Ready);
}

TEST_CASE("M18-D2d2 generic spring binding rejects incomplete and unauthorized models") {
    const auto object = *solo_rig::ParseJson(kModel).value->AsObject();
    REQUIRE(solo_rig::BindGenericSpringModel(object, "blocked.spring.template").error ==
            solo_rig::GenericSpringBindingError::UnauthorizedModel);
    auto invalid = object;
    invalid.erase("decaySeconds");
    REQUIRE(solo_rig::BindGenericSpringModel(invalid, "generic-spring-v1").error ==
            solo_rig::GenericSpringBindingError::InvalidModel);
    invalid = object;
    invalid["modeCount"] = solo_rig::JsonValue(4.5);
    REQUIRE(solo_rig::BindGenericSpringModel(invalid, "generic-spring-v1").error ==
            solo_rig::GenericSpringBindingError::InvalidModel);
    REQUIRE(solo_rig::ParseGenericSpringModel("not-json").error ==
            solo_rig::GenericSpringBindingError::InvalidJson);
    REQUIRE(solo_rig::LowerBehaviorModule(
        {"old-spring", "spring", "blocked.spring.template"}).error ==
        solo_rig::BehaviorLoweringError::BlockedModel);
}
