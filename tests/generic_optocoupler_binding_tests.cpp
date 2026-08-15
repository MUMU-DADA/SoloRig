#include "content/behavior_lowering.hpp"
#include "content/generic_optocoupler_binding.hpp"
#include "test_framework.hpp"

namespace {

constexpr const char* kModel = R"({
  "kind":"generic-optocoupler","modelVersion":1,
  "darkResistance":1000000,"litResistance":100,
  "emitterThreshold":1,"emitterScale":4,"responseExponent":2,
  "attackSeconds":0.01,"releaseSeconds":0.03,"maximumDrive":5,
  "initialIllumination":0.25})";

} // namespace

TEST_CASE("M18-D2d1 generic optocoupler binding parses and lowers the authorized model") {
    const auto parsed = solo_rig::ParseGenericOptocouplerModel(kModel);
    REQUIRE(parsed.error == solo_rig::GenericOptocouplerBindingError::None);
    REQUIRE(parsed.model.has_value());
    REQUIRE(parsed.model->dark_resistance_ohms == 1.0e6);
    REQUIRE(parsed.model->initial_illumination == 0.25);

    const auto object = *solo_rig::ParseJson(kModel).value->AsObject();
    REQUIRE(solo_rig::BindGenericOptocouplerModel(object, "generic-optocoupler-v1").error ==
            solo_rig::GenericOptocouplerBindingError::None);
    const auto plan = solo_rig::LowerBehaviorModule(
        {"optical-control", "optocoupler", "generic-optocoupler-v1"});
    REQUIRE(plan.error == std::nullopt);
    REQUIRE(plan.plan.has_value());
    REQUIRE(plan.plan->kind == solo_rig::BehaviorKind::Optocoupler);
    REQUIRE(plan.plan->status == solo_rig::WhiteboxPlanStatus::Ready);
}

TEST_CASE("M18-D2d1 generic optocoupler binding rejects incomplete and unauthorized models") {
    const auto object = *solo_rig::ParseJson(kModel).value->AsObject();
    REQUIRE(solo_rig::BindGenericOptocouplerModel(
        object, "blocked.optocoupler.template").error ==
        solo_rig::GenericOptocouplerBindingError::UnauthorizedModel);
    auto invalid = object;
    invalid.erase("litResistance");
    REQUIRE(solo_rig::BindGenericOptocouplerModel(
        invalid, "generic-optocoupler-v1").error ==
        solo_rig::GenericOptocouplerBindingError::InvalidModel);
    invalid = object;
    invalid["responseExponent"] = solo_rig::JsonValue(20.0);
    REQUIRE(solo_rig::BindGenericOptocouplerModel(
        invalid, "generic-optocoupler-v1").error ==
        solo_rig::GenericOptocouplerBindingError::InvalidModel);
    REQUIRE(solo_rig::ParseGenericOptocouplerModel("not-json").error ==
            solo_rig::GenericOptocouplerBindingError::InvalidJson);
    REQUIRE(solo_rig::LowerBehaviorModule(
        {"old-optical", "optocoupler", "blocked.optocoupler.template"}).error ==
        solo_rig::BehaviorLoweringError::BlockedModel);
    REQUIRE(solo_rig::LowerBehaviorModule(
        {"spring", "spring", "blocked.spring.template"}).error ==
        solo_rig::BehaviorLoweringError::BlockedModel);
}
