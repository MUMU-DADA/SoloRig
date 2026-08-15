#include "content/behavior_lowering.hpp"
#include "content/generic_vca_binding.hpp"
#include "test_framework.hpp"

namespace {

constexpr const char* kModel = R"({
  "kind":"generic-vca","modelVersion":1,
  "referenceControlVoltage":0,"referenceGain":1,"gainDecibelsPerVolt":6.020599913279624,
  "minimumGain":0.001,"maximumGain":4,"controlSmoothingSeconds":0.005,
  "initialControlVoltage":0,"maximumControlVoltage":5,
  "maximumInput":2,"maximumOutput":4})";

} // namespace

TEST_CASE("M18-D2b2 generic VCA binding parses and lowers the authorized model") {
    const auto parsed = solo_rig::ParseGenericVcaModel(kModel);
    REQUIRE(parsed.error == solo_rig::GenericVcaBindingError::None);
    REQUIRE(parsed.model.has_value());
    REQUIRE(parsed.model->reference_gain == 1.0);

    const auto object = *solo_rig::ParseJson(kModel).value->AsObject();
    REQUIRE(solo_rig::BindGenericVcaModel(object, "generic-vca-v1").error ==
            solo_rig::GenericVcaBindingError::None);
    const auto plan = solo_rig::LowerBehaviorModule({"gain-cell", "vca", "generic-vca-v1"});
    REQUIRE(plan.error == std::nullopt);
    REQUIRE(plan.plan.has_value());
    REQUIRE(plan.plan->kind == solo_rig::BehaviorKind::Vca);
    REQUIRE(plan.plan->status == solo_rig::WhiteboxPlanStatus::Ready);
}

TEST_CASE("M18-D2b2 generic VCA binding rejects incomplete and unauthorized models") {
    const auto object = *solo_rig::ParseJson(kModel).value->AsObject();
    REQUIRE(solo_rig::BindGenericVcaModel(object, "blocked.vca.template").error ==
            solo_rig::GenericVcaBindingError::UnauthorizedModel);
    auto invalid = object;
    invalid.erase("maximumOutput");
    REQUIRE(solo_rig::BindGenericVcaModel(invalid, "generic-vca-v1").error ==
            solo_rig::GenericVcaBindingError::InvalidModel);
    invalid = object;
    invalid["minimumGain"] = solo_rig::JsonValue(8.0);
    REQUIRE(solo_rig::BindGenericVcaModel(invalid, "generic-vca-v1").error ==
            solo_rig::GenericVcaBindingError::InvalidModel);
    REQUIRE(solo_rig::ParseGenericVcaModel("not-json").error ==
            solo_rig::GenericVcaBindingError::InvalidJson);

    const auto blocked = solo_rig::LowerBehaviorModule(
        {"old-vca", "vca", "blocked.vca.template"});
    REQUIRE(blocked.error == solo_rig::BehaviorLoweringError::BlockedModel);
}
