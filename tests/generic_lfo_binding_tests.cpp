#include "content/behavior_lowering.hpp"
#include "content/generic_lfo_binding.hpp"
#include "test_framework.hpp"

namespace {

constexpr const char* kModel = R"({
  "kind":"generic-lfo","modelVersion":1,"waveform":"triangle",
  "frequencyHz":2.5,"outputAmplitude":1,"outputOffset":0,
  "maximumOutput":2,"initialPhaseCycles":0.25,"dutyCycle":0.5})";

} // namespace

TEST_CASE("M18-D2c2 generic LFO binding parses and lowers the authorized model") {
    const auto parsed = solo_rig::ParseGenericLfoModel(kModel);
    REQUIRE(parsed.error == solo_rig::GenericLfoBindingError::None);
    REQUIRE(parsed.model.has_value());
    REQUIRE(parsed.model->waveform == solo_rig::GenericLfoWaveform::Triangle);
    REQUIRE(parsed.model->frequency_hz == 2.5);

    const auto object = *solo_rig::ParseJson(kModel).value->AsObject();
    REQUIRE(solo_rig::BindGenericLfoModel(object, "generic-lfo-v1").error ==
            solo_rig::GenericLfoBindingError::None);
    const auto plan = solo_rig::LowerBehaviorModule({"modulator", "lfo", "generic-lfo-v1"});
    REQUIRE(plan.error == std::nullopt);
    REQUIRE(plan.plan.has_value());
    REQUIRE(plan.plan->kind == solo_rig::BehaviorKind::Lfo);
    REQUIRE(plan.plan->status == solo_rig::WhiteboxPlanStatus::Ready);
}

TEST_CASE("M18-D2c2 generic LFO binding rejects incomplete and unauthorized models") {
    const auto object = *solo_rig::ParseJson(kModel).value->AsObject();
    REQUIRE(solo_rig::BindGenericLfoModel(object, "blocked.lfo.template").error ==
            solo_rig::GenericLfoBindingError::UnauthorizedModel);
    auto invalid = object;
    invalid.erase("dutyCycle");
    REQUIRE(solo_rig::BindGenericLfoModel(invalid, "generic-lfo-v1").error ==
            solo_rig::GenericLfoBindingError::InvalidModel);
    invalid = object;
    invalid["waveform"] = solo_rig::JsonValue("random");
    REQUIRE(solo_rig::BindGenericLfoModel(invalid, "generic-lfo-v1").error ==
            solo_rig::GenericLfoBindingError::InvalidModel);
    REQUIRE(solo_rig::ParseGenericLfoModel("not-json").error ==
            solo_rig::GenericLfoBindingError::InvalidJson);
    REQUIRE(solo_rig::LowerBehaviorModule(
        {"old-lfo", "lfo", "blocked.lfo.template"}).error ==
        solo_rig::BehaviorLoweringError::BlockedModel);
}
