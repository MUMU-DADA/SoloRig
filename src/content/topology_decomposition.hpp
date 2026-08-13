#pragma once

#include "content/whitebox_compiler.hpp"
#include "content/whitebox_runtime.hpp"

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

enum class WhiteboxStageKind {
    Q,
    Series,
    Parallel,
    Rigid,
};

struct WhiteboxStageIr {
    std::string id;
    WhiteboxStageKind kind{WhiteboxStageKind::Q};
    WhiteboxRuntimeBackend backend{WhiteboxRuntimeBackend::Wdf};
    std::vector<std::string> component_ids;
    std::vector<std::string> dependency_stage_ids;
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
    std::optional<WhiteboxCompiledGraph> graph;
};

struct WhiteboxDecompositionResult {
    std::optional<WhiteboxExecutionPlan> plan;
    std::optional<WhiteboxDecompositionError> error;
    WhiteboxTopologyClass classification{WhiteboxTopologyClass::Unknown};
};

enum class WhiteboxStageRuntimeError {
    None,
    NotPrepared,
    UnsupportedAssembly,
    NonFiniteInput,
    SolverFailure,
};

class WhiteboxStageRuntime final {
public:
    WhiteboxStageRuntime() = default;
    WhiteboxStageRuntime(WhiteboxStageRuntime&&) noexcept = default;
    WhiteboxStageRuntime& operator=(WhiteboxStageRuntime&&) noexcept = default;
    WhiteboxStageRuntime(const WhiteboxStageRuntime&) = delete;
    WhiteboxStageRuntime& operator=(const WhiteboxStageRuntime&) = delete;
    [[nodiscard]] WhiteboxStageRuntimeError Process(double input, double& output) noexcept;
    [[nodiscard]] bool SetSampleRate(double sample_rate_hz) noexcept;
    void Reset() noexcept;
    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] WhiteboxRuntimeBackend Backend() const noexcept { return backend_; }

private:
    friend struct WhiteboxStageRuntimePrepareResult;
    friend WhiteboxStageRuntimePrepareResult PrepareWhiteboxStageRuntime(
        const WhiteboxExecutionPlan&, const WhiteboxRuntimeOptions&);

    struct WdfStage {
        WhiteboxStageKind kind{WhiteboxStageKind::Q};
        std::vector<std::size_t> children;
        std::string input_node;
        std::string output_node;
        std::optional<WdfOnePort> port;
        double resistance{};
        double incident{};
        double reflected{};
        double voltage{};
    };

    void ScatterDown(std::size_t index, double incident) noexcept;
    void CaptureOutput(std::size_t index, double input_potential) noexcept;
    void RecomputeWdfResistances() noexcept;

    std::optional<WhiteboxRuntime> mna_runtime_;
    std::vector<WdfStage> wdf_stages_;
    std::string output_node_;
    double captured_output_{};
    bool output_captured_{};
    WhiteboxRuntimeBackend backend_{WhiteboxRuntimeBackend::Mna};
    bool prepared_{false};
};

struct WhiteboxStageRuntimePrepareResult {
    std::optional<WhiteboxStageRuntime> runtime;
    std::optional<WhiteboxStageRuntimeError> error;
};

[[nodiscard]] WhiteboxDecompositionResult AnalyzeWhiteboxGraph(const WhiteboxCompiledGraph& graph);
[[nodiscard]] WhiteboxDecompositionResult AnalyzeWhiteboxGraphV2Json(std::string_view source);
[[nodiscard]] WhiteboxStageRuntimePrepareResult PrepareWhiteboxStageRuntime(
    const WhiteboxExecutionPlan& plan,
    const WhiteboxRuntimeOptions& options = {});

} // namespace solo_rig
