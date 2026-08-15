#include "content/behavior_lowering.hpp"
#include "content/generic_vco_binding.hpp"
#include "test_framework.hpp"

namespace {

constexpr const char* kModel = R"({
  "kind":"generic-vco","modelVersion":1,"waveform":"triangle",
  "baseFrequencyHz":440,"minimumFrequencyHz":20,"maximumFrequencyHz":20000,
  "voltsPerOctave":1,"referenceVoltage":0,"outputAmplitude":1,
  "outputOffset":0,"initialPhaseCycles":0.25,"maximumControlVoltage":10})";

} // namespace

TEST_CASE("M18-D2b1 generic VCO binding parses the complete clean-room contract") {
    const auto parsed = solo_rig::ParseGenericVcoModel(kModel);
    REQUIRE(parsed.error == solo_rig::GenericVcoBindingError::None);
    REQUIRE(parsed.model.has_value());
    REQUIRE(parsed.model->waveform == solo_rig::GenericVcoWaveform::Triangle);
    REQUIRE(parsed.model->base_frequency_hz == 440.0);

    const auto object = *solo_rig::ParseJson(kModel).value->AsObject();
    const auto bound = solo_rig::BindGenericVcoModel(object, "generic-vco-v1");
    REQUIRE(bound.error == solo_rig::GenericVcoBindingError::None);
    const auto plan = solo_rig::LowerBehaviorModule({"oscillator", "vco", "generic-vco-v1"});
    REQUIRE(plan.error == std::nullopt);
    REQUIRE(plan.plan.has_value());
    REQUIRE(plan.plan->kind == solo_rig::BehaviorKind::Vco);
    REQUIRE(plan.plan->status == solo_rig::WhiteboxPlanStatus::Ready);
}

TEST_CASE("M18-D2b1 generic VCO binding rejects incomplete and unauthorized models") {
    const auto object = *solo_rig::ParseJson(kModel).value->AsObject();
    REQUIRE(solo_rig::BindGenericVcoModel(object, "blocked.vco.template").error ==
            solo_rig::GenericVcoBindingError::UnauthorizedModel);
    auto invalid = object;
    invalid["waveform"] = solo_rig::JsonValue("noise");
    REQUIRE(solo_rig::BindGenericVcoModel(invalid, "generic-vco-v1").error ==
            solo_rig::GenericVcoBindingError::InvalidModel);
    invalid = object;
    invalid.erase("voltsPerOctave");
    REQUIRE(solo_rig::BindGenericVcoModel(invalid, "generic-vco-v1").error ==
            solo_rig::GenericVcoBindingError::InvalidModel);
    REQUIRE(solo_rig::ParseGenericVcoModel("not-json").error ==
            solo_rig::GenericVcoBindingError::InvalidJson);

    const auto blocked = solo_rig::LowerBehaviorModule(
        {"old-vco", "vco", "blocked.vco.template"});
    REQUIRE(blocked.error == solo_rig::BehaviorLoweringError::BlockedModel);
}
