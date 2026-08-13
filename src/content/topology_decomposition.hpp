#pragma once

#include "content/whitebox_compiler.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace solo_rig {

enum class WhiteboxPlanStatus {
    Ready,
    Planned,
    Unsupported,
    Blocked,
};

enum class WhiteboxDecompositionError {
    InvalidGraph,
    UnsupportedTopology,
    NonDecomposable,
    FeedbackLoop,
    CrossNetworkBoundary,
};

struct WhiteboxPartitionIr {
    std::string id;
    WhiteboxTopologyClass classification{WhiteboxTopologyClass::Unknown};
    std::vector<std::string> component_ids;
    std::vector<std::string> boundary_nodes;
};

struct WhiteboxStageIr {
    std::string id;
    std::vector<std::string> component_ids;
    std::vector<std::string> input_nodes;
    std::vector<std::string> output_nodes;
    WhiteboxPlanStatus status{WhiteboxPlanStatus::Planned};
    bool feedback{false};
};

struct WhiteboxFeedbackIr {
    WhiteboxPlanStatus status{WhiteboxPlanStatus::Planned};
    bool detected{false};
};

struct WhiteboxBlockwiseIr {
    WhiteboxPlanStatus status{WhiteboxPlanStatus::Planned};
    std::vector<std::string> stage_ids;
    std::vector<std::uint32_t> offsets;
};

struct WhiteboxKMethodIr {
    WhiteboxPlanStatus status{WhiteboxPlanStatus::Planned};
    std::vector<double> coefficients;
};

struct WhiteboxRuntimeAssemblyIr {
    WhiteboxPlanStatus status{WhiteboxPlanStatus::Unsupported};
    std::vector<std::string> stage_ids;
};

struct WhiteboxExecutionPlan {
    WhiteboxPlanStatus status{WhiteboxPlanStatus::Ready};
    WhiteboxTopologyClass classification{WhiteboxTopologyClass::Unknown};
    std::vector<WhiteboxPartitionIr> partitions;
    std::vector<WhiteboxStageIr> stages;
    std::vector<std::string> signal_order;
    WhiteboxFeedbackIr feedback;
    WhiteboxBlockwiseIr blockwise;
    WhiteboxKMethodIr k_method;
    WhiteboxRuntimeAssemblyIr assembly;
};

struct WhiteboxDecompositionResult {
    std::optional<WhiteboxExecutionPlan> plan;
    std::optional<WhiteboxDecompositionError> error;
    WhiteboxTopologyClass classification{WhiteboxTopologyClass::Unknown};
};

enum class WhiteboxStageRuntimeError {
    NotPrepared,
    UnsupportedAssembly,
    NonFiniteInput,
};

class WhiteboxStageRuntime final {
public:
    [[nodiscard]] WhiteboxStageRuntimeError Process(double input, double& output) noexcept;
    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }

private:
    bool prepared_{false};
};

struct WhiteboxStageRuntimePrepareResult {
    std::optional<WhiteboxStageRuntime> runtime;
    std::optional<WhiteboxStageRuntimeError> error;
};

[[nodiscard]] WhiteboxDecompositionResult AnalyzeWhiteboxGraph(const WhiteboxCompiledGraph& graph);
[[nodiscard]] WhiteboxDecompositionResult AnalyzeWhiteboxGraphV2Json(std::string_view source);
[[nodiscard]] WhiteboxStageRuntimePrepareResult PrepareWhiteboxStageRuntime(const WhiteboxExecutionPlan& plan);

} // namespace solo_rig
