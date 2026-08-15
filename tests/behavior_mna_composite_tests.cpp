#include "content/behavior_mna_composite.hpp"
#include "content/behavior_ports.hpp"
#include "test_framework.hpp"

#include "../fixtures/whitebox/m19c_representative_pedal_fixtures.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

solo_rig::BehaviorGraphNodeSpec Node(std::string id, std::string kind, std::string model) {
    return {.id = id,
            .binding = {.module_id = id, .kind = std::move(kind), .model_id = std::move(model)}};
}

solo_rig::BehaviorTypedGraphPlan EnvelopeControlPlan() {
    const auto nodes = std::vector<solo_rig::BehaviorGraphNodeSpec>{
        Node("envelope", "envelope", "generic-envelope-v1")};
    const auto routes = std::vector<solo_rig::BehaviorPortRouteSpec>{
        {"input", "detector", "envelope", "detector"},
        {"envelope", "out", "output", "control"}};
    const auto compiled = solo_rig::CompileTypedBehaviorGraph(nodes, routes);
    REQUIRE(compiled.plan.has_value());
    return *compiled.plan;
}

solo_rig::BehaviorTypedGraphPlan OptocouplerResistancePlan() {
    const auto nodes = std::vector<solo_rig::BehaviorGraphNodeSpec>{
        Node("opto", "optocoupler", "generic-optocoupler-v1")};
    const auto routes = std::vector<solo_rig::BehaviorPortRouteSpec>{
        {"input", "emitter", "opto", "emitter"},
        {"opto", "out", "output", "resistance"}};
    const auto compiled = solo_rig::CompileTypedBehaviorGraph(nodes, routes);
    REQUIRE(compiled.plan.has_value());
    return *compiled.plan;
}

solo_rig::MnaPreparedCircuit PreparedTone() {
    auto circuit = solo_rig::m19c_fixtures::PassiveTone(4U, 0.5);
    circuit.parameter_bindings = {{.model_version = 1U, .parameter_id = "tone",
        .target_id = "tone_pot",
        .property = solo_rig::CircuitParameterBindingProperty::PotentiometerPosition,
        .smoothing_seconds = 0.0}};
    const auto compiled = solo_rig::CompileMnaCircuit(circuit);
    REQUIRE(compiled.circuit.has_value());
    return std::move(*compiled.circuit);
}

} // namespace

TEST_CASE("M20-C2 behavior control output drives caller-owned MNA control") {
    const auto circuit = PreparedTone();
    const auto binding = solo_rig::TypedBehaviorMnaBinding{
        .control_index = 0U,
        .source_domain = solo_rig::BehaviorPortDomain::ControlSignal,
        .source_minimum = 0.0,
        .source_maximum = 1.0,
        .target_minimum = 0.0,
        .target_maximum = 1.0};
    solo_rig::TypedBehaviorMnaRuntime runtime;
    REQUIRE(runtime.Prepare(EnvelopeControlPlan(), std::move(circuit),
        std::span<const solo_rig::TypedBehaviorMnaBinding>(&binding, 1U),
        192000.0, 4U) == solo_rig::TypedBehaviorMnaRuntimeError::None);
    REQUIRE(runtime.BindingCount() == 1U);
    double first{};
    REQUIRE(runtime.Process({.audio = 0.25, .detector = 0.75}, first) ==
        solo_rig::TypedBehaviorMnaRuntimeError::None);
    REQUIRE(std::isfinite(first));
    runtime.Reset();
    double reset{};
    REQUIRE(runtime.Process({.audio = 0.25, .detector = 0.75}, reset) ==
        solo_rig::TypedBehaviorMnaRuntimeError::None);
    REQUIRE(reset == first);
    static_assert(noexcept(std::declval<solo_rig::TypedBehaviorMnaRuntime&>().Process(
        std::declval<const solo_rig::TypedBehaviorInput&>(), std::declval<double&>())));
}

TEST_CASE("M20-C2 resistance output uses an explicit range mapping into MNA") {
    const auto binding = solo_rig::TypedBehaviorMnaBinding{
        .control_index = 0U,
        .source_domain = solo_rig::BehaviorPortDomain::Resistance,
        .source_minimum = 100.0,
        .source_maximum = 1.0e6,
        .target_minimum = 0.0,
        .target_maximum = 1.0};
    solo_rig::TypedBehaviorMnaRuntime runtime;
    REQUIRE(runtime.Prepare(OptocouplerResistancePlan(), PreparedTone(),
        std::span<const solo_rig::TypedBehaviorMnaBinding>(&binding, 1U),
        96000.0, 2U) == solo_rig::TypedBehaviorMnaRuntimeError::None);
    double output{};
    REQUIRE(runtime.Process({.audio = 0.25, .emitter = 2.0}, output) ==
        solo_rig::TypedBehaviorMnaRuntimeError::None);
    REQUIRE(std::isfinite(output));
}

TEST_CASE("M20-C2 composite rejects implicit domains, invalid ranges and duplicate controls") {
    const auto plan = EnvelopeControlPlan();
    const auto audio_binding = solo_rig::TypedBehaviorMnaBinding{
        .control_index = 0U,
        .source_domain = solo_rig::BehaviorPortDomain::AudioSignal,
        .source_minimum = 0.0,
        .source_maximum = 1.0,
        .target_minimum = 0.0,
        .target_maximum = 1.0};
    solo_rig::TypedBehaviorMnaRuntime runtime;
    REQUIRE(runtime.Prepare(plan, PreparedTone(),
        std::span<const solo_rig::TypedBehaviorMnaBinding>(&audio_binding, 1U),
        48000.0, 1U) == solo_rig::TypedBehaviorMnaRuntimeError::InvalidBinding);

    auto invalid_range = audio_binding;
    invalid_range.source_domain = solo_rig::BehaviorPortDomain::ControlSignal;
    invalid_range.target_minimum = -1.0;
    REQUIRE(runtime.Prepare(plan, PreparedTone(),
        std::span<const solo_rig::TypedBehaviorMnaBinding>(&invalid_range, 1U),
        48000.0, 1U) == solo_rig::TypedBehaviorMnaRuntimeError::InvalidRange);

    auto valid_binding = audio_binding;
    valid_binding.source_domain = solo_rig::BehaviorPortDomain::ControlSignal;
    std::array<solo_rig::TypedBehaviorMnaBinding, 2U> duplicate{valid_binding, valid_binding};
    duplicate[1].target_maximum = 0.75;
    REQUIRE(runtime.Prepare(plan, PreparedTone(),
        std::span<const solo_rig::TypedBehaviorMnaBinding>(duplicate),
        48000.0, 1U) == solo_rig::TypedBehaviorMnaRuntimeError::InvalidBinding);
}

TEST_CASE("M20-C2 composite preserves finite and sample-rate failure boundaries") {
    const auto binding = solo_rig::TypedBehaviorMnaBinding{
        .control_index = 0U,
        .source_domain = solo_rig::BehaviorPortDomain::ControlSignal,
        .source_minimum = 0.0,
        .source_maximum = 1.0,
        .target_minimum = 0.0,
        .target_maximum = 1.0};
    solo_rig::TypedBehaviorMnaRuntime runtime;
    REQUIRE(runtime.Prepare(EnvelopeControlPlan(), PreparedTone(),
        std::span<const solo_rig::TypedBehaviorMnaBinding>(&binding, 1U),
        48000.0, 1U) == solo_rig::TypedBehaviorMnaRuntimeError::None);
    double output{};
    REQUIRE(runtime.Process({.audio = std::numeric_limits<double>::quiet_NaN()}, output) ==
        solo_rig::TypedBehaviorMnaRuntimeError::NonFinite);
    REQUIRE(runtime.SetSampleRate(192000.0) == solo_rig::TypedBehaviorMnaRuntimeError::None);
    REQUIRE(runtime.SetSampleRate(std::numeric_limits<double>::infinity()) ==
        solo_rig::TypedBehaviorMnaRuntimeError::InvalidSampleRate);
}
