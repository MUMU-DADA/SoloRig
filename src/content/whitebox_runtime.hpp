#pragma once

#include "content/whitebox_compiler.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace solo_rig {

enum class WhiteboxRuntimeBackend {
    Mna,
    Wdf,
};

enum class WhiteboxRuntimeError {
    None,
    InvalidBackend,
    InvalidOptions,
    InvalidValue,
    InvalidInitialState,
    InvalidTopology,
    UnsupportedBackend,
    UnsupportedComponent,
    SingularMatrix,
    NonFinite,
    NonConvergent,
    NotPrepared,
};

struct WhiteboxRuntimeOptions {
    WhiteboxRuntimeBackend backend{WhiteboxRuntimeBackend::Mna};
    double sample_rate_hz{48000.0};
    std::uint32_t oversampling{1};
    std::uint32_t max_iterations{16};
    double absolute_tolerance{1.0e-9};
    double relative_tolerance{1.0e-6};
    double maximum_initial_state{1.0e9};
};

enum class WhiteboxWdfAdaptorKind {
    Series,
    Parallel,
    RType,
};

struct WhiteboxWdfPlan {
    WhiteboxTopologyClass topology{WhiteboxTopologyClass::Unknown};
    std::vector<WhiteboxWdfAdaptorKind> adaptors;
};

[[nodiscard]] std::optional<WhiteboxWdfPlan> BuildWhiteboxWdfPlan(const WhiteboxCompiledGraph& graph);

struct WhiteboxRuntimePrepareResult;

class WhiteboxRuntime final {
public:
    [[nodiscard]] WhiteboxRuntimeError Process(double input, double& output) noexcept;
    [[nodiscard]] WhiteboxRuntimeError SolveDc(double input, double& output) noexcept;
    void Reset() noexcept;
    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] WhiteboxRuntimeBackend Backend() const noexcept { return backend_; }

private:
    friend struct WhiteboxRuntimePrepareResult;
    friend WhiteboxRuntimePrepareResult PrepareWhiteboxRuntime(const WhiteboxCompiledGraph& graph,
                                                               const WhiteboxRuntimeOptions& options);

    enum class ElementKind {
        Resistor,
        Capacitor,
        Inductor,
        VoltageSource,
        NonlinearAdmittance,
    };

    struct Element {
        ElementKind kind{ElementKind::Resistor};
        std::int32_t positive_node{-1};
        std::int32_t negative_node{-1};
        std::uint32_t branch_index{};
        std::uint32_t state_index{UINT32_MAX};
        double value{};
        double initial{};
    };

    std::vector<Element> elements_;
    std::vector<double> capacitor_history_;
    std::vector<double> inductor_history_;
    std::vector<double> matrix_;
    std::vector<double> rhs_;
    std::vector<double> solution_;
    std::vector<double> previous_;
    std::int32_t input_node_{-1};
    std::int32_t output_node_{-1};
    std::uint32_t input_branch_{UINT32_MAX};
    std::uint32_t matrix_size_{};
    std::uint32_t oversampling_{1};
    std::uint32_t max_iterations_{1};
    double sample_rate_hz_{};
    double absolute_tolerance_{};
    double relative_tolerance_{};
    WhiteboxRuntimeBackend backend_{WhiteboxRuntimeBackend::Mna};
    bool prepared_{};
};

struct WhiteboxRuntimePrepareResult {
    std::optional<WhiteboxRuntime> runtime;
    std::optional<WhiteboxRuntimeError> error;
};

[[nodiscard]] WhiteboxRuntimePrepareResult PrepareWhiteboxRuntime(const WhiteboxCompiledGraph& graph,
                                                                   const WhiteboxRuntimeOptions& options);

} // namespace solo_rig
