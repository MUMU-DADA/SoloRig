#include "m19a1_validation_harness.hpp"

#include "../fixtures/whitebox/m19a1_behavior_chain_fixtures.hpp"
#include "content/behavior_composite.hpp"
#include "content/behavior_composite_runtime.hpp"
#include "content/behavior_envelope_runtime.hpp"
#include "content/behavior_graph.hpp"
#include "content/behavior_optocoupler_runtime.hpp"
#include "content/behavior_runtime.hpp"
#include "content/behavior_typed_runtime.hpp"

#include <cmath>

namespace solo_rig::m19a1_validation {

namespace {

ChainEvidence BaseEvidence() noexcept {
    return {.compiled = false, .prepared = false, .finite_output = false,
        .reset_deterministic = false, .sample_rate_and_oversampling = false};
}

} // namespace

ChainEvidence ValidateTremolo() {
    const auto fixture = m19a1_fixtures::TremoloLfoToVca();
    const auto compiled = CompileTypedBehaviorGraph(fixture.nodes, fixture.routes);
    auto evidence = BaseEvidence();
    evidence.compiled = compiled.plan.has_value() &&
        compiled.error == BehaviorPortCompileError::None;
    if (!evidence.compiled) return evidence;
    TypedBehaviorGraphRuntime runtime;
    evidence.prepared = runtime.Prepare(*compiled.plan, 192000.0, 4U) ==
        TypedBehaviorRuntimeError::None;
    if (!evidence.prepared) return evidence;
    double first{};
    double second{};
    const auto first_result = runtime.Process(0.35, first);
    runtime.Reset();
    const auto second_result = runtime.Process(0.35, second);
    evidence.finite_output = first_result == TypedBehaviorRuntimeError::None &&
        second_result == TypedBehaviorRuntimeError::None && std::isfinite(first) &&
        std::isfinite(second);
    evidence.reset_deterministic = evidence.finite_output && first == second;
    evidence.sample_rate_and_oversampling = runtime.SetSampleRate(96000.0) ==
        TypedBehaviorRuntimeError::None;
    return evidence;
}

ChainEvidence ValidateEnvelopeVcaOpticalLike() {
    const auto fixture = m19a1_fixtures::EnvelopeVcaOpticalLike();
    const auto compiled = CompileEnvelopeVcaComposite(fixture.nodes, fixture.routes);
    auto evidence = BaseEvidence();
    evidence.compiled = compiled.plan.has_value() &&
        compiled.error == BehaviorPortCompileError::None;
    if (!evidence.compiled) return evidence;
    TypedEnvelopeVcaCompositeRuntime runtime;
    evidence.prepared = runtime.Prepare(*compiled.plan, 192000.0, 4U) ==
        TypedEnvelopeVcaRuntimeError::None;
    if (!evidence.prepared) return evidence;
    double first{};
    double second{};
    const auto first_result = runtime.Process(0.4, 0.8, first);
    runtime.Reset();
    const auto second_result = runtime.Process(0.4, 0.8, second);
    evidence.finite_output = first_result == TypedEnvelopeVcaRuntimeError::None &&
        second_result == TypedEnvelopeVcaRuntimeError::None && std::isfinite(first) &&
        std::isfinite(second);
    evidence.reset_deterministic = evidence.finite_output && first == second;
    evidence.sample_rate_and_oversampling = runtime.SetSampleRate(96000.0) ==
        TypedEnvelopeVcaRuntimeError::None;
    return evidence;
}

ChainEvidence ValidateEnvelopeDetector() {
    const auto fixture = m19a1_fixtures::EnvelopeDetectorOutput();
    const auto compiled = CompileTypedBehaviorGraph(fixture.nodes, fixture.routes);
    auto evidence = BaseEvidence();
    evidence.compiled = compiled.plan.has_value() &&
        compiled.error == BehaviorPortCompileError::None;
    if (!evidence.compiled) return evidence;
    TypedEnvelopeRuntime runtime;
    evidence.prepared = runtime.Prepare(*compiled.plan, 192000.0, 4U) ==
        TypedEnvelopeRuntimeError::None;
    if (!evidence.prepared) return evidence;
    double first{};
    double second{};
    const auto first_result = runtime.Process(0.8, first);
    runtime.Reset();
    const auto second_result = runtime.Process(0.8, second);
    evidence.finite_output = first_result == TypedEnvelopeRuntimeError::None &&
        second_result == TypedEnvelopeRuntimeError::None && std::isfinite(first) &&
        std::isfinite(second);
    evidence.reset_deterministic = evidence.finite_output && first == second;
    evidence.sample_rate_and_oversampling = runtime.SetSampleRate(96000.0) ==
        TypedEnvelopeRuntimeError::None;
    return evidence;
}

ChainEvidence ValidateOptocouplerResistance() {
    const auto fixture = m19a1_fixtures::OptocouplerResistance();
    const auto compiled = CompileTypedBehaviorGraph(fixture.nodes, fixture.routes);
    auto evidence = BaseEvidence();
    evidence.compiled = compiled.plan.has_value() &&
        compiled.error == BehaviorPortCompileError::None;
    if (!evidence.compiled) return evidence;
    TypedOptocouplerRuntime runtime;
    evidence.prepared = runtime.Prepare(*compiled.plan, 192000.0, 4U) ==
        TypedOptocouplerRuntimeError::None;
    if (!evidence.prepared) return evidence;
    double first{};
    double second{};
    const auto first_result = runtime.Process(0.8, first);
    runtime.Reset();
    const auto second_result = runtime.Process(0.8, second);
    evidence.finite_output = first_result == TypedOptocouplerRuntimeError::None &&
        second_result == TypedOptocouplerRuntimeError::None && std::isfinite(first) &&
        std::isfinite(second);
    evidence.reset_deterministic = evidence.finite_output && first == second;
    evidence.sample_rate_and_oversampling = runtime.SetSampleRate(96000.0) ==
        TypedOptocouplerRuntimeError::None;
    return evidence;
}

ChainEvidence ValidateBbdVcoSpring() {
    const auto fixture = m19a1_fixtures::BbdVcoSpringSerial();
    const auto compiled = CompileBehaviorGraph(fixture.nodes, fixture.routes);
    auto evidence = BaseEvidence();
    evidence.compiled = compiled.plan.has_value() &&
        compiled.error == BehaviorGraphError::None;
    if (!evidence.compiled) return evidence;
    BehaviorGraphRuntime runtime;
    evidence.prepared = runtime.Prepare(*compiled.plan, 192000.0, 4U) ==
        BehaviorRuntimeError::None;
    if (!evidence.prepared) return evidence;
    double first{};
    double second{};
    const auto first_result = runtime.Process(0.35, first);
    runtime.Reset();
    const auto second_result = runtime.Process(0.35, second);
    evidence.finite_output = first_result == BehaviorRuntimeError::None &&
        second_result == BehaviorRuntimeError::None && std::isfinite(first) &&
        std::isfinite(second);
    evidence.reset_deterministic = evidence.finite_output && first == second;
    evidence.sample_rate_and_oversampling = runtime.SetSampleRate(96000.0) ==
        BehaviorRuntimeError::None;
    return evidence;
}

} // namespace solo_rig::m19a1_validation
