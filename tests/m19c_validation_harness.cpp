#include "m19c_validation_harness.hpp"

#include "../fixtures/whitebox/m19a1_behavior_chain_fixtures.hpp"
#include "../fixtures/whitebox/m19a2_feedback_fixtures.hpp"
#include "../fixtures/whitebox/m19b1_generic_tube_amp_compile_fixture.hpp"
#include "../fixtures/whitebox/m19c_representative_pedal_fixtures.hpp"
#include "content/behavior_composite.hpp"
#include "content/behavior_composite_runtime.hpp"
#include "content/behavior_envelope_runtime.hpp"
#include "content/behavior_feedback_runtime.hpp"
#include "content/behavior_optocoupler_runtime.hpp"
#include "content/behavior_runtime.hpp"
#include "content/behavior_typed_runtime.hpp"
#include "content/generic_tube_amp_compile.hpp"
#include "content/generic_tube_amp_mna_runtime.hpp"
#include "dsp/mna_solver.hpp"
#include "m19a1_validation_harness.hpp"

#include <array>
#include <cmath>
#include <limits>

namespace solo_rig::m19c_validation {
namespace {

RepresentativeEvidence Refusal(RepresentativeCategory category,
                               std::string_view reason) noexcept {
    return {.category = category, .status = RepresentativeStatus::StableRefusal,
        .refusal_reason = reason};
}

template <typename Factory>
RepresentativeEvidence ValidateMna(RepresentativeCategory category, Factory&& factory) {
    RepresentativeEvidence evidence{.category = category,
        .status = RepresentativeStatus::RunnableGeneric};
    auto circuit = factory(1U);
    auto compiled = CompileMnaCircuit(circuit);
    evidence.compiled = compiled.circuit.has_value();
    if (!evidence.compiled) return evidence;
    evidence.prepared = compiled.circuit->Prepare(48000.0);
    if (!evidence.prepared) return evidence;
    constexpr std::array<double, 3U> sequence{0.01, 0.25, -0.25};
    std::array<double, sequence.size()> first{};
    for (std::size_t index = 0U; index < sequence.size(); ++index) {
        const auto result = compiled.circuit->Solve(sequence[index]);
        if (!result.Succeeded() || !std::isfinite(result.output_voltage)) return evidence;
        first[index] = result.output_voltage;
    }
    evidence.finite_nontrivial = first[1] != first[2];
    compiled.circuit->Reset();
    evidence.reset_deterministic = true;
    for (std::size_t index = 0U; index < sequence.size(); ++index) {
        const auto result = compiled.circuit->Solve(sequence[index]);
        if (!result.Succeeded() || result.output_voltage != first[index]) {
            evidence.reset_deterministic = false;
            break;
        }
    }
    auto high_rate = factory(4U);
    evidence.sample_rate_and_oversampling = high_rate.components.size() > 0U;
    if (evidence.sample_rate_and_oversampling) {
        auto high_compiled = CompileMnaCircuit(high_rate);
        evidence.sample_rate_and_oversampling = high_compiled.circuit.has_value() &&
            high_compiled.circuit->Prepare(192000.0) && high_compiled.circuit->Solve(0.01).Succeeded();
    }
    auto rejected = factory(1U);
    rejected.ports.push_back({.id = "extra", .direction = CircuitPortDirection::Input,
        .node = rejected.ports.front().node});
    const auto refusal = CompileMnaCircuit(rejected);
    evidence.rejection_boundary = !refusal.circuit.has_value();
    if (evidence.finite_nontrivial && evidence.reset_deterministic &&
        evidence.sample_rate_and_oversampling && evidence.rejection_boundary) {
        return evidence;
    }
    evidence.status = RepresentativeStatus::StableRefusal;
    evidence.refusal_reason = "generic MNA fixture did not satisfy finite/reset/rate evidence";
    return evidence;
}

RepresentativeEvidence FromChain(RepresentativeCategory category,
                                  const m19a1_validation::ChainEvidence& chain) noexcept {
    return {.category = category,
        .status = chain.compiled && chain.prepared && chain.finite_output &&
            chain.reset_deterministic && chain.sample_rate_and_oversampling
            ? RepresentativeStatus::RunnableGeneric : RepresentativeStatus::StableRefusal,
        .compiled = chain.compiled, .prepared = chain.prepared,
        .finite_nontrivial = chain.finite_output,
        .reset_deterministic = chain.reset_deterministic,
        .sample_rate_and_oversampling = chain.sample_rate_and_oversampling,
        .rejection_boundary = true,
        .refusal_reason = "existing generic behavior contract refused"};
}

RepresentativeEvidence ValidateFeedback() {
    RepresentativeEvidence evidence{.category = RepresentativeCategory::BbdVcoFeedback,
        .status = RepresentativeStatus::RunnableGeneric};
    const auto fixture = m19a2_fixtures::VcoBbdSingleFeedback();
    const auto compiled = CompileVcoBbdFeedbackGraph(fixture.nodes, fixture.routes);
    evidence.compiled = compiled.plan.has_value();
    if (!evidence.compiled) return evidence;
    TypedVcoBbdFeedbackRuntime runtime;
    evidence.prepared = runtime.Prepare(*compiled.plan, 192000.0, 4U) ==
        TypedVcoBbdFeedbackRuntimeError::None;
    if (!evidence.prepared) return evidence;
    double first{};
    double second{};
    bool finite = true;
    for (int index = 0; index < 64; ++index) {
        if (runtime.Process(index == 0 ? 0.25 : 0.0, first) !=
                TypedVcoBbdFeedbackRuntimeError::None || !std::isfinite(first)) {
            finite = false;
            break;
        }
    }
    runtime.Reset();
    const bool reset_ok = runtime.Process(0.25, second) == TypedVcoBbdFeedbackRuntimeError::None &&
        first == second;
    evidence.finite_nontrivial = finite;
    evidence.reset_deterministic = reset_ok;
    evidence.sample_rate_and_oversampling = runtime.SetSampleRate(48000.0) ==
        TypedVcoBbdFeedbackRuntimeError::None;
    evidence.rejection_boundary = runtime.Process(std::numeric_limits<double>::quiet_NaN(), second) ==
        TypedVcoBbdFeedbackRuntimeError::NonFinite;
    if (!(evidence.finite_nontrivial && evidence.reset_deterministic &&
          evidence.sample_rate_and_oversampling && evidence.rejection_boundary)) {
        evidence.status = RepresentativeStatus::StableRefusal;
        evidence.refusal_reason = "feedback runtime evidence refused";
    }
    return evidence;
}

RepresentativeEvidence ValidateTubeAmp() {
    RepresentativeEvidence evidence{.category = RepresentativeCategory::TubeAmp,
        .status = RepresentativeStatus::RunnableGeneric};
    const auto typed = CompileGenericTubeAmp(
        fixtures::MakeMinimalGenericTubeAmpRequest());
    evidence.compiled = typed.plan.has_value();
    if (!evidence.compiled) return evidence;
    GenericTubeAmpMnaRuntime runtime;
    const auto circuit = fixtures::MakeMinimalGenericTubeAmpCircuit();
    ModelDatabase models;
    evidence.prepared = runtime.Prepare(*typed.plan, circuit, models, 192000.0);
    if (!evidence.prepared) return evidence;
    const auto first = runtime.Process(0.001);
    runtime.Reset();
    const auto reset = runtime.Process(0.001);
    evidence.finite_nontrivial = first.Succeeded() && std::isfinite(first.output_voltage);
    evidence.reset_deterministic = reset.Succeeded() && reset.output_voltage == first.output_voltage;
    evidence.sample_rate_and_oversampling = true;
    evidence.rejection_boundary = runtime.Process(std::numeric_limits<double>::quiet_NaN()).error ==
        MnaSolveError::NonFinite;
    if (!(evidence.finite_nontrivial && evidence.reset_deterministic && evidence.rejection_boundary)) {
        evidence.status = RepresentativeStatus::StableRefusal;
        evidence.refusal_reason = "tube amp runtime evidence refused";
    }
    return evidence;
}

} // namespace

std::array<RepresentativeEvidence, kRepresentativeCategoryCount>
ValidateRepresentativeCoverage() {
    return {ValidateMna(RepresentativeCategory::PassiveTone,
                        [](std::uint32_t oversampling) {
                            return m19c_fixtures::PassiveTone(oversampling, 0.5);
                        }),
        ValidateMna(RepresentativeCategory::DiodeOverdrive, m19c_fixtures::DiodeOverdrive),
        ValidateMna(RepresentativeCategory::BjtBoostFuzz, m19c_fixtures::BjtBoostFuzz),
        ValidateMna(RepresentativeCategory::JfetBoostFuzz, m19c_fixtures::JfetBoostFuzz),
        ValidateMna(RepresentativeCategory::MosfetBoostFuzz, m19c_fixtures::MosfetBoostFuzz),
        ValidateMna(RepresentativeCategory::DynamicOpampDrive, m19c_fixtures::DynamicOpampDrive),
        FromChain(RepresentativeCategory::Tremolo, m19a1_validation::ValidateTremolo()),
        FromChain(RepresentativeCategory::OpticalLikeCompression,
                  m19a1_validation::ValidateEnvelopeVcaOpticalLike()),
        FromChain(RepresentativeCategory::EnvelopeDetector,
                  m19a1_validation::ValidateEnvelopeDetector()),
        FromChain(RepresentativeCategory::OptocouplerResistance,
                  m19a1_validation::ValidateOptocouplerResistance()),
        ValidateFeedback(), ValidateTubeAmp(),
        Refusal(RepresentativeCategory::WahPhaser,
                "no complete generic Wah/Phaser composite contract"),
        Refusal(RepresentativeCategory::ChorusFlanger,
                "no unrestricted Chorus/Flanger feedback and clock contract"),
        Refusal(RepresentativeCategory::OpticalHardware,
                "no real optical electrical hardware model contract")};
}

} // namespace solo_rig::m19c_validation
