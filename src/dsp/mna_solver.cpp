#include "dsp/mna_solver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace solo_rig {

namespace {

constexpr std::uint32_t kNoIndex = std::numeric_limits<std::uint32_t>::max();

std::int32_t FindNode(const CircuitNetlist& circuit, std::string_view node) {
    if (node == circuit.reference_ground) {
        return -1;
    }
    std::int32_t index = 0;
    for (const auto& candidate : circuit.nodes) {
        if (candidate == circuit.reference_ground) {
            continue;
        }
        if (candidate == node) {
            return index;
        }
        ++index;
    }
    return -2;
}

void StampConductance(std::vector<double>& matrix,
                      std::uint32_t size,
                      std::int32_t positive,
                      std::int32_t negative,
                      double conductance) noexcept {
    if (positive >= 0) {
        matrix[static_cast<std::size_t>(positive) * size + static_cast<std::size_t>(positive)] += conductance;
    }
    if (negative >= 0) {
        matrix[static_cast<std::size_t>(negative) * size + static_cast<std::size_t>(negative)] += conductance;
    }
    if (positive >= 0 && negative >= 0) {
        matrix[static_cast<std::size_t>(positive) * size + static_cast<std::size_t>(negative)] -= conductance;
        matrix[static_cast<std::size_t>(negative) * size + static_cast<std::size_t>(positive)] -= conductance;
    }
}

void StampCurrent(std::vector<double>& rhs,
                  std::int32_t positive,
                  std::int32_t negative,
                  double current) noexcept {
    if (positive >= 0) {
        rhs[static_cast<std::size_t>(positive)] += current;
    }
    if (negative >= 0) {
        rhs[static_cast<std::size_t>(negative)] -= current;
    }
}

void StampVoltageSource(std::vector<double>& matrix,
                        std::vector<double>& rhs,
                        std::uint32_t size,
                        std::int32_t positive,
                        std::int32_t negative,
                        std::uint32_t branch,
                        double voltage) noexcept {
    if (positive >= 0) {
        matrix[static_cast<std::size_t>(positive) * size + branch] += 1.0;
        matrix[static_cast<std::size_t>(branch) * size + static_cast<std::size_t>(positive)] += 1.0;
    }
    if (negative >= 0) {
        matrix[static_cast<std::size_t>(negative) * size + branch] -= 1.0;
        matrix[static_cast<std::size_t>(branch) * size + static_cast<std::size_t>(negative)] -= 1.0;
    }
    rhs[branch] += voltage;
}

MnaSolveError SolveDense(std::vector<double>& matrix,
                         std::vector<double>& rhs,
                         std::vector<double>& solution,
                         std::uint32_t size) noexcept {
    constexpr double kPivotEpsilon = 1.0e-12;
    for (std::uint32_t column = 0; column < size; ++column) {
        std::uint32_t pivot = column;
        double pivot_value = std::fabs(matrix[static_cast<std::size_t>(column) * size + column]);
        for (std::uint32_t row = column + 1U; row < size; ++row) {
            const double candidate = std::fabs(matrix[static_cast<std::size_t>(row) * size + column]);
            if (candidate > pivot_value) {
                pivot = row;
                pivot_value = candidate;
            }
        }
        if (!std::isfinite(pivot_value) || pivot_value <= kPivotEpsilon) {
            return MnaSolveError::SingularMatrix;
        }
        if (pivot != column) {
            for (std::uint32_t index = column; index < size; ++index) {
                std::swap(matrix[static_cast<std::size_t>(column) * size + index],
                          matrix[static_cast<std::size_t>(pivot) * size + index]);
            }
            std::swap(rhs[column], rhs[pivot]);
        }
        for (std::uint32_t row = column + 1U; row < size; ++row) {
            const double factor = matrix[static_cast<std::size_t>(row) * size + column] /
                matrix[static_cast<std::size_t>(column) * size + column];
            if (!std::isfinite(factor)) {
                return MnaSolveError::NonFinite;
            }
            matrix[static_cast<std::size_t>(row) * size + column] = 0.0;
            for (std::uint32_t index = column + 1U; index < size; ++index) {
                matrix[static_cast<std::size_t>(row) * size + index] -=
                    factor * matrix[static_cast<std::size_t>(column) * size + index];
            }
            rhs[row] -= factor * rhs[column];
        }
    }
    for (std::int32_t row = static_cast<std::int32_t>(size) - 1; row >= 0; --row) {
        double value = rhs[static_cast<std::size_t>(row)];
        for (std::uint32_t column = static_cast<std::uint32_t>(row + 1); column < size; ++column) {
            value -= matrix[static_cast<std::size_t>(row) * size + column] * solution[column];
        }
        const double diagonal = matrix[static_cast<std::size_t>(row) * size + static_cast<std::size_t>(row)];
        if (!std::isfinite(value) || !std::isfinite(diagonal) || std::fabs(diagonal) <= kPivotEpsilon) {
            return MnaSolveError::SingularMatrix;
        }
        solution[static_cast<std::size_t>(row)] = value / diagonal;
        if (!std::isfinite(solution[static_cast<std::size_t>(row)])) {
            return MnaSolveError::NonFinite;
        }
    }
    return MnaSolveError::None;
}

} // namespace

bool MnaPreparedCircuit::Prepare(double sample_rate_hz) noexcept {
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0 || matrix_size_ == 0U ||
        matrix_.size() != static_cast<std::size_t>(matrix_size_) * matrix_size_ || rhs_.size() != matrix_size_ ||
        solution_.size() != matrix_size_) {
        prepared_ = false;
        return false;
    }
    sample_rate_hz_ = sample_rate_hz;
    Reset();
    prepared_ = true;
    return true;
}

void MnaPreparedCircuit::Reset() noexcept {
    for (auto& history : capacitor_history_) {
        history = 0.0;
    }
    for (const auto& element : elements_) {
        if (element.history_index != kNoIndex) {
            capacitor_history_[element.history_index] = element.initial;
        }
    }
    std::fill(matrix_.begin(), matrix_.end(), 0.0);
    std::fill(rhs_.begin(), rhs_.end(), 0.0);
    std::fill(solution_.begin(), solution_.end(), 0.0);
}

MnaSolveResult MnaPreparedCircuit::Solve(double input_voltage) noexcept {
    if (!prepared_) {
        return {.error = MnaSolveError::NotPrepared};
    }
    if (!std::isfinite(input_voltage)) {
        return {.error = MnaSolveError::NonFinite};
    }
    MnaSolveResult result;
    const double conductance_scale = sample_rate_hz_ * static_cast<double>(oversampling_);
    for (std::uint32_t substep = 0; substep < oversampling_; ++substep) {
        std::fill(matrix_.begin(), matrix_.end(), 0.0);
        std::fill(rhs_.begin(), rhs_.end(), 0.0);
        for (const auto& element : elements_) {
            switch (element.type) {
            case CircuitComponentType::Resistor:
                StampConductance(matrix_, matrix_size_, element.positive_node, element.negative_node, 1.0 / element.value);
                break;
            case CircuitComponentType::Capacitor: {
                const double conductance = element.value * conductance_scale;
                StampConductance(matrix_, matrix_size_, element.positive_node, element.negative_node, conductance);
                StampCurrent(rhs_, element.positive_node, element.negative_node,
                             conductance * capacitor_history_[element.history_index]);
                break;
            }
            case CircuitComponentType::VoltageSource:
                StampVoltageSource(matrix_, rhs_, matrix_size_, element.positive_node, element.negative_node,
                                   element.branch_index, element.value);
                break;
            case CircuitComponentType::Diode:
            case CircuitComponentType::Potentiometer:
            case CircuitComponentType::Switch:
                return {.error = MnaSolveError::NonFinite};
            }
        }
        if (input_branch_ != kNoIndex) {
            StampVoltageSource(matrix_, rhs_, matrix_size_, input_node_, -1, input_branch_, input_voltage);
        }
        result.error = SolveDense(matrix_, rhs_, solution_, matrix_size_);
        if (!result.Succeeded()) {
            return result;
        }
        for (const auto& element : elements_) {
            if (element.history_index != kNoIndex) {
                const double positive = element.positive_node >= 0 ? solution_[element.positive_node] : 0.0;
                const double negative = element.negative_node >= 0 ? solution_[element.negative_node] : 0.0;
                capacitor_history_[element.history_index] = positive - negative;
            }
        }
        result.output_voltage = output_node_ >= 0 ? solution_[output_node_] : 0.0;
    }
    if (!std::isfinite(result.output_voltage)) {
        result.error = MnaSolveError::NonFinite;
    }
    return result;
}

MnaCompileResult CompileMnaCircuit(const CircuitNetlist& circuit) {
    if (circuit.solver.method != "modified-nodal-analysis" || circuit.solver.integration != "backward-euler" ||
        circuit.solver.oversampling == 0U || circuit.solver.oversampling > 16U) {
        return {.circuit = std::nullopt, .error = MnaCompileError::InvalidSolver};
    }
    const auto output_port = std::find_if(circuit.ports.begin(), circuit.ports.end(), [](const CircuitPort& port) {
        return port.direction == CircuitPortDirection::Output;
    });
    if (output_port == circuit.ports.end()) {
        return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
    }
    const auto second_output = std::find_if(std::next(output_port), circuit.ports.end(), [](const CircuitPort& port) {
        return port.direction == CircuitPortDirection::Output;
    });
    if (second_output != circuit.ports.end()) {
        return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
    }
    const auto input_port = std::find_if(circuit.ports.begin(), circuit.ports.end(), [](const CircuitPort& port) {
        return port.direction == CircuitPortDirection::Input;
    });
    const auto second_input = input_port == circuit.ports.end()
        ? circuit.ports.end()
        : std::find_if(std::next(input_port), circuit.ports.end(), [](const CircuitPort& port) {
              return port.direction == CircuitPortDirection::Input;
          });
    if (second_input != circuit.ports.end()) {
        return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
    }

    MnaPreparedCircuit prepared;
    for (const auto& component : circuit.components) {
        if (component.type != CircuitComponentType::Resistor && component.type != CircuitComponentType::Capacitor &&
            component.type != CircuitComponentType::VoltageSource) {
            return {.circuit = std::nullopt, .error = MnaCompileError::UnsupportedComponent};
        }
        if (!component.value.has_value() || !std::isfinite(*component.value) ||
            ((component.type == CircuitComponentType::Resistor || component.type == CircuitComponentType::Capacitor) &&
             *component.value <= 0.0)) {
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
        }
    }

    std::uint32_t non_ground_nodes{};
    for (const auto& node : circuit.nodes) {
        if (node != circuit.reference_ground) {
            ++non_ground_nodes;
        }
    }
    const auto voltage_source_count = static_cast<std::uint32_t>(std::count_if(
        circuit.components.begin(), circuit.components.end(), [](const CircuitComponent& component) {
            return component.type == CircuitComponentType::VoltageSource;
        })) + static_cast<std::uint32_t>(circuit.supplies.size()) + (input_port == circuit.ports.end() ? 0U : 1U);
    if (non_ground_nodes + voltage_source_count == 0U) {
        return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
    }
    prepared.matrix_size_ = non_ground_nodes + voltage_source_count;
    prepared.matrix_.assign(static_cast<std::size_t>(prepared.matrix_size_) * prepared.matrix_size_, 0.0);
    prepared.rhs_.assign(prepared.matrix_size_, 0.0);
    prepared.solution_.assign(prepared.matrix_size_, 0.0);
    prepared.oversampling_ = circuit.solver.oversampling;
    prepared.output_node_ = FindNode(circuit, output_port->node);
    if (prepared.output_node_ == -2) {
        return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
    }
    if (input_port != circuit.ports.end()) {
        prepared.input_node_ = FindNode(circuit, input_port->node);
        if (prepared.input_node_ < 0) {
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
        }
    }
    std::uint32_t branch{};
    std::uint32_t history{};
    for (const auto& component : circuit.components) {
        const auto positive = FindNode(circuit, component.pins[0]);
        const auto negative = FindNode(circuit, component.pins[1]);
        if (positive == -2 || negative == -2 || positive == negative) {
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
        }
        MnaPreparedCircuit::Element element{.type = component.type,
                                            .positive_node = positive,
                                            .negative_node = negative,
                                            .branch_index = kNoIndex,
                                            .history_index = kNoIndex,
                                            .value = *component.value,
                                            .initial = component.initial.value_or(0.0)};
        if (component.type == CircuitComponentType::VoltageSource) {
            element.branch_index = non_ground_nodes + branch++;
        }
        if (component.type == CircuitComponentType::Capacitor) {
            element.history_index = history++;
        }
        prepared.elements_.push_back(element);
    }
    for (const auto& supply : circuit.supplies) {
        const auto positive = FindNode(circuit, supply.positive_node);
        const auto negative = FindNode(circuit, supply.negative_node);
        if (positive == -2 || negative == -2 || positive == negative) {
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
        }
        prepared.elements_.push_back({.type = CircuitComponentType::VoltageSource,
                                      .positive_node = positive,
                                      .negative_node = negative,
                                      .branch_index = non_ground_nodes + branch++,
                                      .history_index = kNoIndex,
                                      .value = supply.voltage,
                                      .initial = 0.0});
    }
    if (input_port != circuit.ports.end()) {
        prepared.input_branch_ = non_ground_nodes + branch++;
    }
    if (branch != voltage_source_count) {
        return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
    }
    prepared.capacitor_history_.assign(history, 0.0);
    return {.circuit = std::move(prepared), .error = std::nullopt};
}

} // namespace solo_rig
