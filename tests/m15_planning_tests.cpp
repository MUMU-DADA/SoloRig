#include "content/behavior_lowering.hpp"
#include "content/topology_decomposition.hpp"
#include "content/whitebox_compiler.hpp"
#include "content/json.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

std::string ReadFixture(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" / "whitebox" / name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open M15 fixture: " + path.string());
    }
    std::ostringstream source;
    source << stream.rdbuf();
    return source.str();
}

solo_rig::WhiteboxCompiledGraph Compile(std::string_view fixture) {
    const auto result = solo_rig::CompileWhiteboxGraphV2Json(ReadFixture(fixture));
    if (!result.graph.has_value()) {
        throw std::runtime_error("M15 fixture did not compile");
    }
    return *result.graph;
}

std::string BuildLongSeries(std::size_t component_count) {
    std::ostringstream source;
    source << R"({"version":2,"schema":"circuit","referenceGround":"0","nodes":["in")";
    for (std::size_t index = 1; index < component_count; ++index) {
        source << ",\"n" << index << "\"";
    }
    source << R"(,"0"],"ports":[{"id":"input","direction":"input","node":"in"},)"
           << R"({"id":"output","direction":"output","node":"n1"}],"components":[)";
    for (std::size_t index = 0; index < component_count; ++index) {
        if (index != 0U) {
            source << ',';
        }
        const auto first = index == 0U ? std::string("in") : "n" + std::to_string(index);
        const auto second = index + 1U == component_count ? std::string("0")
                                                          : "n" + std::to_string(index + 1U);
        source << "{\"id\":\"r" << index << "\",\"kind\":\"resistor\",\"pins\":[\""
               << first << "\",\"" << second << "\"],\"value\":1000}";
    }
    source << R"(],"subcircuits":[],"modelReferences":[],"backend":{"preferred":"mna"}})";
    return source.str();
}

} // namespace

TEST_CASE("M15 creates deterministic series and parallel decomposition IR") {
    const auto series = solo_rig::AnalyzeWhiteboxGraphV2Json(ReadFixture("m12_series.json"));
    REQUIRE(series.error == std::nullopt);
    REQUIRE(series.plan.has_value());
    REQUIRE(series.classification == solo_rig::WhiteboxTopologyClass::Series);
    REQUIRE(series.plan->partitions.size() == 1U);
    REQUIRE(series.plan->partitions[0].component_ids.size() == 2U);
    REQUIRE(series.plan->stages.size() == 3U);
    REQUIRE(series.plan->stages.back().kind == solo_rig::WhiteboxStageKind::Series);
    REQUIRE(series.plan->signal_order.size() == series.plan->stages.size());
    REQUIRE(series.plan->blockwise.status == solo_rig::WhiteboxPlanStatus::Ready);
    REQUIRE(series.plan->k_method.status == solo_rig::WhiteboxPlanStatus::Planned);
    REQUIRE(series.plan->assembly.status == solo_rig::WhiteboxPlanStatus::Ready);
    REQUIRE(series.plan->feedback.status == solo_rig::WhiteboxPlanStatus::Ready);

    const auto parallel = solo_rig::AnalyzeWhiteboxGraph(Compile("m12_parallel.json"));
    REQUIRE(parallel.error == std::nullopt);
    REQUIRE(parallel.plan.has_value());
    REQUIRE(parallel.classification == solo_rig::WhiteboxTopologyClass::Parallel);
    REQUIRE(parallel.plan->partitions[0].classification == solo_rig::WhiteboxTopologyClass::Parallel);
}

TEST_CASE("M18-B lowers a series-parallel cycle without misreporting directed feedback") {
    const auto spqr = solo_rig::AnalyzeWhiteboxGraphV2Json(ReadFixture("m12_spqr_pending.json"));
    REQUIRE(spqr.plan.has_value());
    REQUIRE(spqr.classification == solo_rig::WhiteboxTopologyClass::Spqr);
    REQUIRE(spqr.error == std::nullopt);
    REQUIRE(spqr.plan->stages.back().kind == solo_rig::WhiteboxStageKind::Parallel);
    REQUIRE(std::any_of(spqr.plan->stages.begin(), spqr.plan->stages.end(), [](const auto& stage) {
        return stage.kind == solo_rig::WhiteboxStageKind::Series;
    }));
    REQUIRE(!spqr.plan->feedback.detected);

    const auto non_decomposable =
        solo_rig::AnalyzeWhiteboxGraphV2Json(ReadFixture("m15_non_decomposable.json"));
    REQUIRE(non_decomposable.plan.has_value());
    REQUIRE(non_decomposable.classification == solo_rig::WhiteboxTopologyClass::Spqr);
    REQUIRE(non_decomposable.error == std::nullopt);
    REQUIRE(non_decomposable.plan->stages.back().kind == solo_rig::WhiteboxStageKind::Rigid);

    const auto rigid = solo_rig::AnalyzeWhiteboxGraphV2Json(ReadFixture("m12_rigid_pending.json"));
    REQUIRE(!rigid.plan.has_value());
    REQUIRE(rigid.classification == solo_rig::WhiteboxTopologyClass::Rigid);
    REQUIRE(rigid.error == solo_rig::WhiteboxDecompositionError::NonDecomposable);
}

TEST_CASE("M18-B keeps passive graph cycles and rejects unresolved cross-network boundaries") {
    const auto cycle = solo_rig::AnalyzeWhiteboxGraphV2Json(ReadFixture("m15_feedback_loop.json"));
    REQUIRE(cycle.plan.has_value());
    REQUIRE(cycle.classification == solo_rig::WhiteboxTopologyClass::Spqr);
    REQUIRE(cycle.error == std::nullopt);
    REQUIRE(!cycle.plan->feedback.detected);

    const auto cross = solo_rig::AnalyzeWhiteboxGraphV2Json(ReadFixture("m15_cross_network.json"));
    REQUIRE(!cross.plan.has_value());
    REQUIRE(cross.error == solo_rig::WhiteboxDecompositionError::CrossNetworkBoundary);
}

TEST_CASE("M15 behavior lowering keeps BBD VCO VCA and spring bindings blocked") {
    const auto fixture = solo_rig::ParseJson(ReadFixture("m15_behavior_bindings.json"));
    REQUIRE(fixture.value.has_value());
    const auto* object = fixture.value->AsObject();
    REQUIRE(object != nullptr);
    const auto* modules = object->at("modules").AsArray();
    REQUIRE(modules != nullptr);
    for (const auto& module : *modules) {
        const auto* entry = module.AsObject();
        REQUIRE(entry != nullptr);
        solo_rig::BehaviorBindingRequest request{
            *entry->at("moduleId").AsString(),
            *entry->at("kind").AsString(),
            *entry->at("modelId").AsString()};
        const auto result = solo_rig::LowerBehaviorModule(request);
        REQUIRE(!result.plan.has_value());
        REQUIRE(result.error == solo_rig::BehaviorLoweringError::BlockedModel);
    }
    const auto unknown = solo_rig::LowerBehaviorModule({"unknown", "future-effect", "missing"});
    REQUIRE(unknown.error == solo_rig::BehaviorLoweringError::UnknownBehavior);
    const auto unbound = solo_rig::LowerBehaviorModule({"bbd-without-model", "bbd", ""});
    REQUIRE(unbound.error == solo_rig::BehaviorLoweringError::BlockedModel);
}

TEST_CASE("M18-B assembles a nested Q-P-S WDF tree with deterministic routing") {
    const auto first = solo_rig::AnalyzeWhiteboxGraphV2Json(ReadFixture("m18b_nested_sp.json"));
    const auto reordered = solo_rig::AnalyzeWhiteboxGraphV2Json(ReadFixture("m18b_nested_sp_reordered.json"));
    REQUIRE(first.plan.has_value());
    REQUIRE(reordered.plan.has_value());
    REQUIRE(first.classification == solo_rig::WhiteboxTopologyClass::Spqr);
    REQUIRE(first.plan->signal_order == reordered.plan->signal_order);
    REQUIRE(first.plan->assembly.status == solo_rig::WhiteboxPlanStatus::Ready);
    REQUIRE(first.plan->stages.back().kind == solo_rig::WhiteboxStageKind::Series);
    REQUIRE(std::any_of(first.plan->stages.begin(), first.plan->stages.end(), [](const auto& stage) {
        return stage.kind == solo_rig::WhiteboxStageKind::Parallel;
    }));
    REQUIRE(std::count_if(first.plan->stages.begin(), first.plan->stages.end(), [](const auto& stage) {
        return stage.kind == solo_rig::WhiteboxStageKind::Q;
    }) == 3);

    auto prepared = solo_rig::PrepareWhiteboxStageRuntime(*first.plan);
    REQUIRE(prepared.error == std::nullopt);
    REQUIRE(prepared.runtime.has_value());
    REQUIRE(prepared.runtime->Backend() == solo_rig::WhiteboxRuntimeBackend::Wdf);
    auto mna_options = solo_rig::WhiteboxRuntimeOptions{};
    mna_options.backend = solo_rig::WhiteboxRuntimeBackend::Mna;
    auto mna = solo_rig::PrepareWhiteboxRuntime(*first.plan->graph, mna_options);
    REQUIRE(mna.runtime.has_value());
    double output{};
    for (const double input : {-1.0, -0.25, 0.0, 0.5, 1.0}) {
        double reference{};
        REQUIRE(prepared.runtime->Process(input, output) == solo_rig::WhiteboxStageRuntimeError::None);
        REQUIRE(mna.runtime->Process(input, reference) == solo_rig::WhiteboxRuntimeError::None);
        REQUIRE(std::fabs(output - reference) < 1.0e-12);
    }
    REQUIRE(prepared.runtime->SetSampleRate(96000.0));
    REQUIRE(!prepared.runtime->SetSampleRate(std::numeric_limits<double>::quiet_NaN()));
    prepared.runtime->Reset();
    REQUIRE(prepared.runtime->Process(1.0, output) == solo_rig::WhiteboxStageRuntimeError::None);
    REQUIRE(std::fabs(output - 0.5) < 1.0e-12);
    REQUIRE(prepared.runtime->Process(std::numeric_limits<double>::quiet_NaN(), output) ==
            solo_rig::WhiteboxStageRuntimeError::NonFiniteInput);
    static_assert(noexcept(std::declval<solo_rig::WhiteboxStageRuntime&>().Process(
        0.0, std::declval<double&>())));
}

TEST_CASE("M18-B routes structurally different residual R graphs through MNA") {
    for (const auto fixture : {"m18b_rigid_bridge.json", "m18b_rigid_lattice.json"}) {
        const auto analyzed = solo_rig::AnalyzeWhiteboxGraphV2Json(ReadFixture(fixture));
        REQUIRE(analyzed.error == std::nullopt);
        REQUIRE(analyzed.plan.has_value());
        REQUIRE(analyzed.plan->stages.size() == 1U);
        REQUIRE(analyzed.plan->stages.back().kind == solo_rig::WhiteboxStageKind::Rigid);
        REQUIRE(analyzed.plan->stages.back().backend == solo_rig::WhiteboxRuntimeBackend::Mna);
        auto prepared = solo_rig::PrepareWhiteboxStageRuntime(*analyzed.plan);
        REQUIRE(prepared.error == std::nullopt);
        REQUIRE(prepared.runtime.has_value());
        REQUIRE(prepared.runtime->Backend() == solo_rig::WhiteboxRuntimeBackend::Mna);
        double output{};
        REQUIRE(prepared.runtime->Process(1.0, output) == solo_rig::WhiteboxStageRuntimeError::None);
        REQUIRE(std::fabs(output - 0.5) < 1.0e-12);
    }
}

TEST_CASE("M18-B nested dynamic WDF state matches MNA and resets deterministically") {
    const auto analyzed = solo_rig::AnalyzeWhiteboxGraphV2Json(ReadFixture("m18b_nested_dynamic.json"));
    REQUIRE(analyzed.plan.has_value());
    auto wdf_prepared = solo_rig::PrepareWhiteboxStageRuntime(*analyzed.plan);
    REQUIRE(wdf_prepared.runtime.has_value());
    auto mna_prepared = solo_rig::PrepareWhiteboxRuntime(*analyzed.plan->graph, {});
    REQUIRE(mna_prepared.runtime.has_value());
    std::array<double, 8> first_pass{};
    for (std::size_t index = 0; index < first_pass.size(); ++index) {
        const double input = index < 5U ? 1.0 : -0.25;
        double wdf_output{};
        double mna_output{};
        REQUIRE(wdf_prepared.runtime->Process(input, wdf_output) ==
                solo_rig::WhiteboxStageRuntimeError::None);
        REQUIRE(mna_prepared.runtime->Process(input, mna_output) == solo_rig::WhiteboxRuntimeError::None);
        REQUIRE(std::fabs(wdf_output - mna_output) < 1.0e-10);
        first_pass[index] = wdf_output;
    }
    wdf_prepared.runtime->Reset();
    for (std::size_t index = 0; index < first_pass.size(); ++index) {
        const double input = index < 5U ? 1.0 : -0.25;
        double output{};
        REQUIRE(wdf_prepared.runtime->Process(input, output) ==
                solo_rig::WhiteboxStageRuntimeError::None);
        REQUIRE(std::fabs(output - first_pass[index]) < 1.0e-12);
    }
}

TEST_CASE("M18-B retains a stable refusal contract for unprepared and unsupported assembly") {
    const auto plan = solo_rig::AnalyzeWhiteboxGraph(Compile("m12_series.json"));
    REQUIRE(plan.plan.has_value());
    REQUIRE(plan.plan->assembly.stage_ids.size() == plan.plan->stages.size());
    REQUIRE(plan.plan->assembly.status == solo_rig::WhiteboxPlanStatus::Ready);
    const auto prepared = solo_rig::PrepareWhiteboxStageRuntime(*plan.plan);
    REQUIRE(!prepared.runtime.has_value());
    REQUIRE(prepared.error == solo_rig::WhiteboxStageRuntimeError::UnsupportedAssembly);
    solo_rig::WhiteboxStageRuntime runtime;
    double output{};
    REQUIRE(runtime.Process(0.0, output) == solo_rig::WhiteboxStageRuntimeError::NotPrepared);
    static_assert(noexcept(runtime.Process(0.0, output)));

    auto executable = solo_rig::AnalyzeWhiteboxGraphV2Json(ReadFixture("m18b_nested_sp.json"));
    REQUIRE(executable.plan.has_value());
    executable.plan->signal_order.back() = "tampered-stage";
    const auto tampered = solo_rig::PrepareWhiteboxStageRuntime(*executable.plan);
    REQUIRE(!tampered.runtime.has_value());
    REQUIRE(tampered.error == solo_rig::WhiteboxStageRuntimeError::UnsupportedAssembly);

    executable = solo_rig::AnalyzeWhiteboxGraphV2Json(ReadFixture("m18b_nested_sp.json"));
    auto invalid_options = solo_rig::WhiteboxRuntimeOptions{};
    invalid_options.sample_rate_hz = std::numeric_limits<double>::quiet_NaN();
    const auto invalid = solo_rig::PrepareWhiteboxStageRuntime(*executable.plan, invalid_options);
    REQUIRE(!invalid.runtime.has_value());
    REQUIRE(invalid.error == solo_rig::WhiteboxStageRuntimeError::UnsupportedAssembly);
}

TEST_CASE("M18-B rejects an oversized stage graph before recursive runtime assembly") {
    const auto source = BuildLongSeries(257U);
    const auto compiled = solo_rig::CompileWhiteboxGraphV2Json(source);
    REQUIRE(compiled.graph.has_value());
    const auto oversized = solo_rig::AnalyzeWhiteboxGraphV2Json(source);
    REQUIRE(!oversized.plan.has_value());
    REQUIRE(oversized.error == solo_rig::WhiteboxDecompositionError::UnsupportedTopology);
}
