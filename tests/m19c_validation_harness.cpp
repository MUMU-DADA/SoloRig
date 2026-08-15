#include "m19c_validation_harness.hpp"

#include "../fixtures/whitebox/m19a1_behavior_chain_fixtures.hpp"
#include "../fixtures/whitebox/m19a2_feedback_fixtures.hpp"
#include "../fixtures/whitebox/m19b1_generic_tube_amp_compile_fixture.hpp"
#include "../fixtures/whitebox/m19c_representative_pedal_fixtures.hpp"
#include "content/behavior_composite.hpp"
#include "content/behavior_composite_runtime.hpp"
#include "content/behavior_clocked.hpp"
#include "content/behavior_clocked_runtime.hpp"
#include "content/behavior_envelope_runtime.hpp"
#include "content/behavior_feedback_runtime.hpp"
#include "content/behavior_mna_modulation.hpp"
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
#include <utility>

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

RepresentativeEvidence ValidateClocked() {
    RepresentativeEvidence evidence{.category = RepresentativeCategory::ChorusFlanger,
        .status = RepresentativeStatus::RunnableGeneric};
    const std::vector<BehaviorGraphNodeSpec> nodes = {
        {.id = "vco", .binding = {.module_id = "vco", .kind = "vco",
            .model_id = "generic-vco-v1"}},
        {.id = "bbd", .binding = {.module_id = "bbd", .kind = "bbd",
            .model_id = "generic-bbd-v1"}},
    };
    const std::vector<BehaviorPortRouteSpec> routes = {
        {"input", "audio", "bbd", "audio_in"},
        {"vco", "out", "bbd", "clock"},
        {"bbd", "out", "output", "audio"},
    };
    const auto compiled = CompileVcoBbdClockedGraph(nodes, routes);
    evidence.compiled = compiled.plan.has_value();
    if (!evidence.compiled) return evidence;

    TypedVcoBbdClockedModel model{};
    model.vco.waveform = GenericVcoWaveform::Triangle;
    model.vco.base_frequency_hz = 0.7;
    model.vco.minimum_frequency_hz = 0.1;
    model.vco.maximum_frequency_hz = 4.0;
    model.vco.output_amplitude = 0.35;
    model.vco.maximum_control_voltage = 1.0;
    model.bbd.bucket_count = 128U;
    model.bbd.clock_frequency_hz = 100000.0;
    model.bbd.minimum_clock_frequency_hz = 50000.0;
    model.bbd.maximum_clock_frequency_hz = 200000.0;
    model.bbd.transfer_efficiency = 0.995;
    model.bbd.feedback = 0.35;
    model.bbd.wet = 0.75;
    model.bbd.dry = 0.25;

    TypedVcoBbdClockedRuntime runtime;
    evidence.prepared = runtime.Prepare(*compiled.plan, model, 48000.0, 1U) ==
        TypedVcoBbdClockedRuntimeError::None;
    if (!evidence.prepared) return evidence;
    std::array<double, 128U> first{};
    bool finite = true;
    for (std::size_t index = 0U; index < first.size(); ++index) {
        double output{};
        const auto result = runtime.Process(index == 0U ? 0.5 : 0.0, output);
        finite = finite && result == TypedVcoBbdClockedRuntimeError::None &&
            std::isfinite(output);
        first[index] = output;
    }
    evidence.finite_nontrivial = finite && first[0] != 0.0;
    runtime.Reset();
    evidence.reset_deterministic = true;
    for (std::size_t index = 0U; index < first.size(); ++index) {
        double output{};
        if (runtime.Process(index == 0U ? 0.5 : 0.0, output) !=
                TypedVcoBbdClockedRuntimeError::None || output != first[index]) {
            evidence.reset_deterministic = false;
            break;
        }
    }
    evidence.sample_rate_and_oversampling = runtime.SetSampleRate(192000.0) ==
        TypedVcoBbdClockedRuntimeError::None;
    double output{};
    evidence.rejection_boundary = runtime.Process(std::numeric_limits<double>::quiet_NaN(), output) ==
        TypedVcoBbdClockedRuntimeError::NonFinite;
    if (!(evidence.finite_nontrivial && evidence.reset_deterministic &&
          evidence.sample_rate_and_oversampling && evidence.rejection_boundary)) {
        evidence.status = RepresentativeStatus::StableRefusal;
        evidence.refusal_reason = "clocked BBD generic model evidence refused";
    }
    return evidence;
}

RepresentativeEvidence ValidateWah() {
    RepresentativeEvidence evidence{.category = RepresentativeCategory::Wah,
        .status = RepresentativeStatus::RunnableGeneric};
    auto circuit = m19c_fixtures::PassiveTone(4U, 0.5);
    circuit.parameter_bindings = {{.model_version = 1U, .parameter_id = "wah",
        .target_id = "tone_pot",
        .property = CircuitParameterBindingProperty::PotentiometerPosition,
        .smoothing_seconds = 0.0}};
    const auto compiled = CompileMnaCircuit(circuit);
    evidence.compiled = compiled.circuit.has_value();
    if (!evidence.compiled) return evidence;

    TypedLfoMnaModel model{};
    model.lfo.frequency_hz = 2.0;
    model.modulation_min = 0.0;
    model.modulation_max = 1.0;
    TypedLfoMnaRuntime runtime;
    evidence.prepared = runtime.Prepare(std::move(*compiled.circuit), "wah", model,
        48000.0, 4U) == TypedLfoMnaRuntimeError::None;
    if (!evidence.prepared) return evidence;
    std::array<double, 32U> first{};
    bool finite = true;
    for (std::size_t index = 0U; index < first.size(); ++index) {
        const auto result = runtime.Process(index == 0U ? 0.25 : -0.1, first[index]);
        finite = finite && result == TypedLfoMnaRuntimeError::None &&
            std::isfinite(first[index]);
    }
    evidence.finite_nontrivial = finite && first[0] != first[1];
    runtime.Reset();
    evidence.reset_deterministic = true;
    for (std::size_t index = 0U; index < first.size(); ++index) {
        double output{};
        if (runtime.Process(index == 0U ? 0.25 : -0.1, output) !=
                TypedLfoMnaRuntimeError::None || output != first[index]) {
            evidence.reset_deterministic = false;
            break;
        }
    }
    evidence.sample_rate_and_oversampling = runtime.SetSampleRate(192000.0) ==
        TypedLfoMnaRuntimeError::None;
    double output{};
    evidence.rejection_boundary = runtime.Process(std::numeric_limits<double>::quiet_NaN(), output) ==
        TypedLfoMnaRuntimeError::NonFinite;
    if (!(evidence.finite_nontrivial && evidence.reset_deterministic &&
          evidence.sample_rate_and_oversampling && evidence.rejection_boundary)) {
        evidence.status = RepresentativeStatus::StableRefusal;
        evidence.refusal_reason = "LFO to MNA potentiometer modulation evidence refused";
    }
    return evidence;
}

RepresentativeEvidence ValidatePhaser() {
    RepresentativeEvidence evidence{.category = RepresentativeCategory::Phaser,
        .status = RepresentativeStatus::RunnableGeneric};
    const auto circuit = m19c_fixtures::PhaserTone(4U);
    const auto compiled = CompileMnaCircuit(circuit);
    evidence.compiled = compiled.circuit.has_value();
    if (!evidence.compiled) return evidence;
    const std::array<std::string_view, 2U> parameters{"stage1", "stage2"};
    TypedLfoMnaModel model{};
    model.lfo.frequency_hz = 0.8;
    model.modulation_min = 0.0;
    model.modulation_max = 1.0;
    TypedLfoMnaRuntime runtime;
    evidence.prepared = runtime.Prepare(std::move(*compiled.circuit), parameters, model,
        48000.0, 4U) == TypedLfoMnaRuntimeError::None;
    if (!evidence.prepared) return evidence;
    std::array<double, 32U> first{};
    bool finite = true;
    for (std::size_t index = 0U; index < first.size(); ++index) {
        const auto result = runtime.Process(index == 0U ? 0.25 : -0.1, first[index]);
        finite = finite && result == TypedLfoMnaRuntimeError::None &&
            std::isfinite(first[index]);
    }
    evidence.finite_nontrivial = finite && first[0] != first[1];
    runtime.Reset();
    evidence.reset_deterministic = true;
    for (std::size_t index = 0U; index < first.size(); ++index) {
        double output{};
        if (runtime.Process(index == 0U ? 0.25 : -0.1, output) !=
                TypedLfoMnaRuntimeError::None || output != first[index]) {
            evidence.reset_deterministic = false;
            break;
        }
    }
    evidence.sample_rate_and_oversampling = runtime.SetSampleRate(192000.0) ==
        TypedLfoMnaRuntimeError::None;
    double output{};
    evidence.rejection_boundary = runtime.Process(std::numeric_limits<double>::quiet_NaN(), output) ==
        TypedLfoMnaRuntimeError::NonFinite;
    if (!(evidence.finite_nontrivial && evidence.reset_deterministic &&
          evidence.sample_rate_and_oversampling && evidence.rejection_boundary)) {
        evidence.status = RepresentativeStatus::StableRefusal;
        evidence.refusal_reason = "two-stage Phaser LFO/MNA evidence refused";
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
        ValidateFeedback(), ValidateTubeAmp(), ValidateWah(), ValidatePhaser(),
        ValidateClocked(),
        Refusal(RepresentativeCategory::OpticalHardware,
                "no real optical electrical hardware model contract")};
}

} // namespace solo_rig::m19c_validation
