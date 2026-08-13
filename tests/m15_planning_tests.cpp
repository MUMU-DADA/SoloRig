#include "content/behavior_lowering.hpp"
#include "content/topology_decomposition.hpp"
#include "content/whitebox_compiler.hpp"
#include "content/json.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

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

} // namespace

TEST_CASE("M15 creates deterministic series and parallel decomposition IR") {
    const auto series = solo_rig::AnalyzeWhiteboxGraphV2Json(ReadFixture("m12_series.json"));
    REQUIRE(series.error == std::nullopt);
    REQUIRE(series.plan.has_value());
    REQUIRE(series.classification == solo_rig::WhiteboxTopologyClass::Series);
    REQUIRE(series.plan->partitions.size() == 1U);
    REQUIRE(series.plan->partitions[0].component_ids.size() == 2U);
    REQUIRE(series.plan->stages.size() == 2U);
    REQUIRE(series.plan->signal_order.size() == series.plan->stages.size());
    REQUIRE(series.plan->blockwise.status == solo_rig::WhiteboxPlanStatus::Planned);
    REQUIRE(series.plan->k_method.status == solo_rig::WhiteboxPlanStatus::Planned);
    REQUIRE(series.plan->assembly.status == solo_rig::WhiteboxPlanStatus::Unsupported);
    REQUIRE(series.plan->feedback.status == solo_rig::WhiteboxPlanStatus::Planned);

    const auto parallel = solo_rig::AnalyzeWhiteboxGraph(Compile("m12_parallel.json"));
    REQUIRE(parallel.error == std::nullopt);
    REQUIRE(parallel.plan.has_value());
    REQUIRE(parallel.classification == solo_rig::WhiteboxTopologyClass::Parallel);
    REQUIRE(parallel.plan->partitions[0].classification == solo_rig::WhiteboxTopologyClass::Parallel);
}

TEST_CASE("M15 preserves SPQR and rigid classifications with stable refusal errors") {
    const auto spqr = solo_rig::AnalyzeWhiteboxGraphV2Json(ReadFixture("m12_spqr_pending.json"));
    REQUIRE(!spqr.plan.has_value());
    REQUIRE(spqr.classification == solo_rig::WhiteboxTopologyClass::Spqr);
    REQUIRE(spqr.error == solo_rig::WhiteboxDecompositionError::FeedbackLoop);

    const auto non_decomposable =
        solo_rig::AnalyzeWhiteboxGraphV2Json(ReadFixture("m15_non_decomposable.json"));
    REQUIRE(!non_decomposable.plan.has_value());
    REQUIRE(non_decomposable.classification == solo_rig::WhiteboxTopologyClass::Spqr);
    REQUIRE(non_decomposable.error == solo_rig::WhiteboxDecompositionError::NonDecomposable);

    const auto rigid = solo_rig::AnalyzeWhiteboxGraphV2Json(ReadFixture("m12_rigid_pending.json"));
    REQUIRE(!rigid.plan.has_value());
    REQUIRE(rigid.classification == solo_rig::WhiteboxTopologyClass::Rigid);
    REQUIRE(rigid.error == solo_rig::WhiteboxDecompositionError::NonDecomposable);
}

TEST_CASE("M15 rejects feedback loops before stage assembly") {
    const auto feedback = solo_rig::AnalyzeWhiteboxGraphV2Json(ReadFixture("m15_feedback_loop.json"));
    REQUIRE(!feedback.plan.has_value());
    REQUIRE(feedback.classification == solo_rig::WhiteboxTopologyClass::Spqr);
    REQUIRE(feedback.error == solo_rig::WhiteboxDecompositionError::FeedbackLoop);

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

TEST_CASE("M15 exposes a planned stage boundary and real-time refusal contract") {
    const auto plan = solo_rig::AnalyzeWhiteboxGraph(Compile("m12_series.json"));
    REQUIRE(plan.plan.has_value());
    REQUIRE(plan.plan->assembly.stage_ids.size() == plan.plan->stages.size());
    REQUIRE(plan.plan->assembly.status == solo_rig::WhiteboxPlanStatus::Unsupported);
    const auto prepared = solo_rig::PrepareWhiteboxStageRuntime(*plan.plan);
    REQUIRE(!prepared.runtime.has_value());
    REQUIRE(prepared.error == solo_rig::WhiteboxStageRuntimeError::UnsupportedAssembly);
    solo_rig::WhiteboxStageRuntime runtime;
    double output{};
    REQUIRE(runtime.Process(0.0, output) == solo_rig::WhiteboxStageRuntimeError::NotPrepared);
    static_assert(noexcept(runtime.Process(0.0, output)));
}
