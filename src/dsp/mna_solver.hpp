#pragma once

#include "content/circuit_v2.hpp"
#include "content/model_database.hpp"
#include "core/realtime_contract.hpp"
#include "dsp/nonlinear_device.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace solo_rig {

struct MnaCompileResult;

enum class MnaCompileError {
    InvalidTopology,
    UnsupportedComponent,
    InvalidValue,
    InvalidSolver,
    InvalidModel,
    MissingModelAuthorization,
    InvalidConvergence,
};

enum class MnaSolveError {
    None,
    NotPrepared,
    SingularMatrix,
    NonFinite,
    NonConvergent,
};

struct MnaSolveResult {
    MnaSolveError error{MnaSolveError::None};
    double output_voltage{};

    [[nodiscard]] bool Succeeded() const noexcept {
        return error == MnaSolveError::None;
    }
};

class MnaPreparedCircuit final {
public:
    MnaPreparedCircuit() = default;

    [[nodiscard]] bool Prepare(double sample_rate_hz) noexcept;
    // Compile/Prepare owns all storage; Solve is the real-time-safe sample step
    // and only mutates preallocated matrices, RHS, solution and dynamic histories.
    [[nodiscard]] MnaSolveResult Solve(double input_voltage) noexcept;
    [[nodiscard]] static constexpr RealtimeContract ProcessContract() noexcept {
        return DefaultRealtimeContract();
    }
    void Reset() noexcept;

public:
    struct Element {
        CircuitComponentType type{CircuitComponentType::Resistor};
        std::int32_t positive_node{-1};
        std::int32_t negative_node{-1};
        std::int32_t control_positive_node{-1};
        std::int32_t control_negative_node{-1};
        std::int32_t bjt_base_node{-1};
        std::int32_t jfet_gate_node{-1};
        std::int32_t mosfet_gate_node{-1};
        std::uint32_t branch_index{};
        std::uint32_t history_index{UINT32_MAX};
        std::uint32_t inductor_history_index{UINT32_MAX};
        double value{};
        double initial{};
        NonlinearDeviceModel nonlinear_model{};
        double controlled_gain{};
        double controlled_minimum{};
        double controlled_maximum{};
        bool controlled_has_limits{};
        NonlinearBjtModel nonlinear_bjt_model{};
        NonlinearJfetModel nonlinear_jfet_model{};
        NonlinearMosfetModel nonlinear_mosfet_model{};
    };

private:
    friend struct MnaCompileResult;

    std::vector<Element> elements_;
    std::vector<double> capacitor_history_;
    std::vector<double> inductor_history_;
    std::vector<double> matrix_;
    std::vector<double> rhs_;
    std::vector<double> solution_;
    std::vector<double> newton_previous_;
    std::int32_t input_node_{-1};
    std::int32_t output_node_{-1};
    std::uint32_t input_branch_{UINT32_MAX};
    std::uint32_t matrix_size_{};
    std::uint32_t oversampling_{1};
    double sample_rate_hz_{};
    std::uint32_t max_iterations_{1};
    double absolute_tolerance_{1.0e-9};
    double relative_tolerance_{1.0e-6};
    bool prepared_{};

    friend MnaCompileResult CompileMnaCircuit(const CircuitNetlist& circuit);
};

struct MnaCompileResult {
    std::optional<MnaPreparedCircuit> circuit;
    std::optional<MnaCompileError> error;
};

[[nodiscard]] MnaCompileResult CompileMnaCircuit(const CircuitNetlist& circuit);
[[nodiscard]] MnaCompileResult CompileMnaCircuit(const CircuitNetlist& circuit,
                                                 const ModelDatabase& model_database);

} // namespace solo_rig
