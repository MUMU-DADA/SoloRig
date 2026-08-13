#pragma once

#include "content/circuit_v2.hpp"

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
};

enum class MnaSolveError {
    None,
    NotPrepared,
    SingularMatrix,
    NonFinite,
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
    // and only mutates preallocated matrices, RHS, solution and capacitor history.
    [[nodiscard]] MnaSolveResult Solve(double input_voltage) noexcept;
    void Reset() noexcept;

private:
    friend struct MnaCompileResult;

    struct Element {
        CircuitComponentType type{CircuitComponentType::Resistor};
        std::int32_t positive_node{-1};
        std::int32_t negative_node{-1};
        std::uint32_t branch_index{};
        std::uint32_t history_index{UINT32_MAX};
        double value{};
        double initial{};
    };

    std::vector<Element> elements_;
    std::vector<double> capacitor_history_;
    std::vector<double> matrix_;
    std::vector<double> rhs_;
    std::vector<double> solution_;
    std::int32_t input_node_{-1};
    std::int32_t output_node_{-1};
    std::uint32_t input_branch_{UINT32_MAX};
    std::uint32_t matrix_size_{};
    std::uint32_t oversampling_{1};
    double sample_rate_hz_{};
    bool prepared_{};

    friend MnaCompileResult CompileMnaCircuit(const CircuitNetlist& circuit);
};

struct MnaCompileResult {
    std::optional<MnaPreparedCircuit> circuit;
    std::optional<MnaCompileError> error;
};

[[nodiscard]] MnaCompileResult CompileMnaCircuit(const CircuitNetlist& circuit);

} // namespace solo_rig
