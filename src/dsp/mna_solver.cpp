#include "dsp/mna_solver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace solo_rig {

namespace {

constexpr std::uint32_t kNoIndex = std::numeric_limits<std::uint32_t>::max();

std::optional<double> ModelParameter(const ModelAuditRecord& model, std::string_view id) {
    const auto parameter = std::find_if(model.parameters.begin(), model.parameters.end(),
                                        [id](const auto& candidate) { return candidate.id == id; });
    if (parameter == model.parameters.end()) {
        return std::nullopt;
    }
    return parameter->default_value;
}

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

// Backward-Euler inductor companion in branch-current MNA form:
// v(+) - v(-) - (L/dt)i = -(L/dt)i_previous.
void StampInductor(std::vector<double>& matrix,
                   std::vector<double>& rhs,
                   std::uint32_t size,
                   std::int32_t positive,
                   std::int32_t negative,
                   std::uint32_t branch,
                   double inductance_over_dt,
                   double previous_current) noexcept {
    if (positive >= 0) {
        matrix[static_cast<std::size_t>(positive) * size + branch] += 1.0;
        matrix[static_cast<std::size_t>(branch) * size + static_cast<std::size_t>(positive)] += 1.0;
    }
    if (negative >= 0) {
        matrix[static_cast<std::size_t>(negative) * size + branch] -= 1.0;
        matrix[static_cast<std::size_t>(branch) * size + static_cast<std::size_t>(negative)] -= 1.0;
    }
    matrix[static_cast<std::size_t>(branch) * size + branch] -= inductance_over_dt;
    rhs[branch] -= inductance_over_dt * previous_current;
}

void StampVccs(std::vector<double>& matrix,
               std::vector<double>& rhs,
               std::uint32_t size,
               const MnaPreparedCircuit::Element& element,
               const std::vector<double>& guess) noexcept {
    const auto control_positive = element.control_positive_node >= 0
        ? guess[static_cast<std::size_t>(element.control_positive_node)] : 0.0;
    const auto control_negative = element.control_negative_node >= 0
        ? guess[static_cast<std::size_t>(element.control_negative_node)] : 0.0;
    const auto control_voltage = control_positive - control_negative;
    const auto raw_current = element.controlled_gain * control_voltage;
    if (element.controlled_has_limits &&
        (raw_current < element.controlled_minimum || raw_current > element.controlled_maximum)) {
        StampCurrent(rhs, element.positive_node, element.negative_node,
                     std::clamp(raw_current, element.controlled_minimum, element.controlled_maximum));
        return;
    }
    if (element.positive_node >= 0 && element.control_positive_node >= 0) {
        matrix[static_cast<std::size_t>(element.positive_node) * size +
               static_cast<std::size_t>(element.control_positive_node)] -= element.controlled_gain;
    }
    if (element.positive_node >= 0 && element.control_negative_node >= 0) {
        matrix[static_cast<std::size_t>(element.positive_node) * size +
               static_cast<std::size_t>(element.control_negative_node)] += element.controlled_gain;
    }
    if (element.negative_node >= 0 && element.control_positive_node >= 0) {
        matrix[static_cast<std::size_t>(element.negative_node) * size +
               static_cast<std::size_t>(element.control_positive_node)] += element.controlled_gain;
    }
    if (element.negative_node >= 0 && element.control_negative_node >= 0) {
        matrix[static_cast<std::size_t>(element.negative_node) * size +
               static_cast<std::size_t>(element.control_negative_node)] -= element.controlled_gain;
    }
}

void StampIdealOpAmp(std::vector<double>& matrix,
                     std::vector<double>& rhs,
                     std::uint32_t size,
                     const MnaPreparedCircuit::Element& element,
                     const std::vector<double>& guess) noexcept {
    StampVoltageSource(matrix, rhs, size, element.positive_node, element.negative_node,
                       element.branch_index, 0.0);
    const auto control_positive = element.control_positive_node >= 0
        ? guess[static_cast<std::size_t>(element.control_positive_node)] : 0.0;
    const auto control_negative = element.control_negative_node >= 0
        ? guess[static_cast<std::size_t>(element.control_negative_node)] : 0.0;
    const auto raw_output = element.controlled_gain * (control_positive - control_negative);
    if (element.controlled_has_limits &&
        (raw_output < element.controlled_minimum || raw_output > element.controlled_maximum)) {
        rhs[element.branch_index] += std::clamp(raw_output, element.controlled_minimum, element.controlled_maximum);
        return;
    }
    if (element.control_positive_node >= 0) {
        matrix[static_cast<std::size_t>(element.branch_index) * size +
               static_cast<std::size_t>(element.control_positive_node)] -= element.controlled_gain;
    }
    if (element.control_negative_node >= 0) {
        matrix[static_cast<std::size_t>(element.branch_index) * size +
               static_cast<std::size_t>(element.control_negative_node)] += element.controlled_gain;
    }
}

void StampOta(std::vector<double>& matrix,
              std::vector<double>& rhs,
              std::uint32_t size,
              const MnaPreparedCircuit::Element& element,
              const std::vector<double>& guess) noexcept {
    const auto control_positive = element.control_positive_node >= 0
        ? guess[static_cast<std::size_t>(element.control_positive_node)] : 0.0;
    const auto control_negative = element.control_negative_node >= 0
        ? guess[static_cast<std::size_t>(element.control_negative_node)] : 0.0;
    const auto evaluation = EvaluateNonlinearAmplifier(
        element.nonlinear_amplifier_model, control_positive, control_negative);
    if (!evaluation.Succeeded()) {
        StampCurrent(rhs, element.positive_node, element.negative_node,
                     std::numeric_limits<double>::quiet_NaN());
        return;
    }
    const double controls[2] = {control_positive, control_negative};
    const std::int32_t nodes[2] = {element.control_positive_node, element.control_negative_node};
    double equivalent_current = evaluation.output;
    for (std::size_t column = 0; column < 2U; ++column) {
        equivalent_current -= evaluation.jacobian[column] * controls[column];
        if (element.positive_node >= 0 && nodes[column] >= 0) {
            matrix[static_cast<std::size_t>(element.positive_node) * size +
                   static_cast<std::size_t>(nodes[column])] -= evaluation.jacobian[column];
        }
        if (element.negative_node >= 0 && nodes[column] >= 0) {
            matrix[static_cast<std::size_t>(element.negative_node) * size +
                   static_cast<std::size_t>(nodes[column])] += evaluation.jacobian[column];
        }
    }
    StampCurrent(rhs, element.positive_node, element.negative_node, equivalent_current);
}

void StampOpAmp(std::vector<double>& matrix,
                std::vector<double>& rhs,
                std::uint32_t size,
                const MnaPreparedCircuit::Element& element,
                const std::vector<double>& guess) noexcept {
    StampVoltageSource(matrix, rhs, size, element.positive_node, element.negative_node,
                       element.branch_index, 0.0);
    const auto control_positive = element.control_positive_node >= 0
        ? guess[static_cast<std::size_t>(element.control_positive_node)] : 0.0;
    const auto control_negative = element.control_negative_node >= 0
        ? guess[static_cast<std::size_t>(element.control_negative_node)] : 0.0;
    const auto evaluation = EvaluateNonlinearAmplifier(
        element.nonlinear_amplifier_model, control_positive, control_negative);
    if (!evaluation.Succeeded()) {
        rhs[element.branch_index] = std::numeric_limits<double>::quiet_NaN();
        return;
    }
    const double controls[2] = {control_positive, control_negative};
    const std::int32_t nodes[2] = {element.control_positive_node, element.control_negative_node};
    double equivalent_voltage = evaluation.output;
    for (std::size_t column = 0; column < 2U; ++column) {
        equivalent_voltage -= evaluation.jacobian[column] * controls[column];
        if (nodes[column] >= 0) {
            matrix[static_cast<std::size_t>(element.branch_index) * size +
                   static_cast<std::size_t>(nodes[column])] -= evaluation.jacobian[column];
        }
    }
    rhs[element.branch_index] += equivalent_voltage;
}

void StampBjt(std::vector<double>& matrix,
              std::vector<double>& rhs,
              std::uint32_t size,
              const MnaPreparedCircuit::Element& element,
              const std::vector<double>& guess) noexcept {
    const auto collector = element.positive_node >= 0 ? guess[static_cast<std::size_t>(element.positive_node)] : 0.0;
    const auto base = element.bjt_base_node >= 0 ? guess[static_cast<std::size_t>(element.bjt_base_node)] : 0.0;
    const auto emitter = element.negative_node >= 0 ? guess[static_cast<std::size_t>(element.negative_node)] : 0.0;
    const auto evaluation = EvaluateNonlinearBjt(element.nonlinear_bjt_model, collector, base, emitter);
    if (!evaluation.Succeeded()) {
        StampConductance(matrix, size, element.positive_node, element.negative_node,
                         std::numeric_limits<double>::quiet_NaN());
        return;
    }
    const std::int32_t nodes[3] = {element.positive_node, element.bjt_base_node, element.negative_node};
    const double voltages[3] = {collector, base, emitter};
    for (std::size_t row = 0; row < 3U; ++row) {
        if (nodes[row] < 0) {
            continue;
        }
        double equivalent_current = evaluation.currents[row];
        for (std::size_t column = 0; column < 3U; ++column) {
            equivalent_current -= evaluation.jacobian[row][column] * voltages[column];
            if (nodes[column] >= 0) {
                matrix[static_cast<std::size_t>(nodes[row]) * size + static_cast<std::size_t>(nodes[column])] +=
                    evaluation.jacobian[row][column];
            }
        }
        rhs[static_cast<std::size_t>(nodes[row])] -= equivalent_current;
    }
}

void StampJfet(std::vector<double>& matrix,
               std::vector<double>& rhs,
               std::uint32_t size,
               const MnaPreparedCircuit::Element& element,
               const std::vector<double>& guess) noexcept {
    const auto drain = element.positive_node >= 0 ? guess[static_cast<std::size_t>(element.positive_node)] : 0.0;
    const auto gate = element.jfet_gate_node >= 0 ? guess[static_cast<std::size_t>(element.jfet_gate_node)] : 0.0;
    const auto source = element.negative_node >= 0 ? guess[static_cast<std::size_t>(element.negative_node)] : 0.0;
    const auto evaluation = EvaluateNonlinearJfet(element.nonlinear_jfet_model, drain, gate, source);
    if (!evaluation.Succeeded()) {
        StampConductance(matrix, size, element.positive_node, element.negative_node,
                         std::numeric_limits<double>::quiet_NaN());
        return;
    }
    const std::int32_t nodes[3] = {element.positive_node, element.jfet_gate_node, element.negative_node};
    const double voltages[3] = {drain, gate, source};
    for (std::size_t row = 0; row < 3U; ++row) {
        if (nodes[row] < 0) {
            continue;
        }
        double equivalent_current = evaluation.currents[row];
        for (std::size_t column = 0; column < 3U; ++column) {
            equivalent_current -= evaluation.jacobian[row][column] * voltages[column];
            if (nodes[column] >= 0) {
                matrix[static_cast<std::size_t>(nodes[row]) * size + static_cast<std::size_t>(nodes[column])] +=
                    evaluation.jacobian[row][column];
            }
        }
        rhs[static_cast<std::size_t>(nodes[row])] -= equivalent_current;
    }
}

void StampMosfet(std::vector<double>& matrix,
                 std::vector<double>& rhs,
                 std::uint32_t size,
                 const MnaPreparedCircuit::Element& element,
                 const std::vector<double>& guess) noexcept {
    const auto drain = element.positive_node >= 0 ? guess[static_cast<std::size_t>(element.positive_node)] : 0.0;
    const auto gate = element.mosfet_gate_node >= 0 ? guess[static_cast<std::size_t>(element.mosfet_gate_node)] : 0.0;
    const auto source = element.negative_node >= 0 ? guess[static_cast<std::size_t>(element.negative_node)] : 0.0;
    const auto evaluation = EvaluateNonlinearMosfet(element.nonlinear_mosfet_model, drain, gate, source);
    if (!evaluation.Succeeded()) {
        StampConductance(matrix, size, element.positive_node, element.negative_node,
                         std::numeric_limits<double>::quiet_NaN());
        return;
    }
    const std::int32_t nodes[3] = {element.positive_node, element.mosfet_gate_node, element.negative_node};
    const double voltages[3] = {drain, gate, source};
    for (std::size_t row = 0; row < 3U; ++row) {
        if (nodes[row] < 0) {
            continue;
        }
        double equivalent_current = evaluation.currents[row];
        for (std::size_t column = 0; column < 3U; ++column) {
            equivalent_current -= evaluation.jacobian[row][column] * voltages[column];
            if (nodes[column] >= 0) {
                matrix[static_cast<std::size_t>(nodes[row]) * size + static_cast<std::size_t>(nodes[column])] +=
                    evaluation.jacobian[row][column];
            }
        }
        rhs[static_cast<std::size_t>(nodes[row])] -= equivalent_current;
    }
}

void StampTriode(std::vector<double>& matrix,
                 std::vector<double>& rhs,
                 std::uint32_t size,
                 const MnaPreparedCircuit::Element& element,
                 const std::vector<double>& guess) noexcept {
    const auto plate = element.positive_node >= 0 ? guess[static_cast<std::size_t>(element.positive_node)] : 0.0;
    const auto grid = element.triode_grid_node >= 0 ? guess[static_cast<std::size_t>(element.triode_grid_node)] : 0.0;
    const auto cathode = element.negative_node >= 0 ? guess[static_cast<std::size_t>(element.negative_node)] : 0.0;
    const auto evaluation = EvaluateNonlinearTriode(element.nonlinear_triode_model, plate, grid, cathode);
    if (!evaluation.Succeeded()) {
        StampConductance(matrix, size, element.positive_node, element.negative_node,
                         std::numeric_limits<double>::quiet_NaN());
        return;
    }
    const std::int32_t nodes[3] = {element.positive_node, element.triode_grid_node, element.negative_node};
    const double voltages[3] = {plate, grid, cathode};
    for (std::size_t row = 0; row < 3U; ++row) {
        if (nodes[row] < 0) {
            continue;
        }
        double equivalent_current = evaluation.currents[row];
        for (std::size_t column = 0; column < 3U; ++column) {
            equivalent_current -= evaluation.jacobian[row][column] * voltages[column];
            if (nodes[column] >= 0) {
                matrix[static_cast<std::size_t>(nodes[row]) * size + static_cast<std::size_t>(nodes[column])] +=
                    evaluation.jacobian[row][column];
            }
        }
        rhs[static_cast<std::size_t>(nodes[row])] -= equivalent_current;
    }
}

void StampPentode(std::vector<double>& matrix, std::vector<double>& rhs, std::uint32_t size,
                  const MnaPreparedCircuit::Element& element, const std::vector<double>& guess) noexcept {
    const auto voltage = [&guess](std::int32_t node) { return node >= 0 ? guess[static_cast<std::size_t>(node)] : 0.0; };
    const double plate = voltage(element.positive_node);
    const double screen = voltage(element.pentode_screen_node);
    const double grid = voltage(element.pentode_grid_node);
    const double cathode = voltage(element.negative_node);
    const auto evaluation = EvaluateNonlinearPentode(
        element.nonlinear_pentode_model, plate, screen, grid, cathode);
    if (!evaluation.Succeeded()) {
        StampConductance(matrix, size, element.positive_node, element.negative_node,
                         std::numeric_limits<double>::quiet_NaN());
        return;
    }
    const std::int32_t nodes[4] = {element.positive_node, element.pentode_screen_node,
                                   element.pentode_grid_node, element.negative_node};
    const double voltages[4] = {plate, screen, grid, cathode};
    for (std::size_t row = 0; row < 4U; ++row) {
        if (nodes[row] < 0) continue;
        double equivalent_current = evaluation.currents[row];
        for (std::size_t column = 0; column < 4U; ++column) {
            equivalent_current -= evaluation.jacobian[row][column] * voltages[column];
            if (nodes[column] >= 0) {
                matrix[static_cast<std::size_t>(nodes[row]) * size + static_cast<std::size_t>(nodes[column])] +=
                    evaluation.jacobian[row][column];
            }
        }
        rhs[static_cast<std::size_t>(nodes[row])] -= equivalent_current;
    }
}

void StampDiode(std::vector<double>& matrix,
                std::vector<double>& rhs,
                std::uint32_t size,
                const MnaPreparedCircuit::Element& element,
                const std::vector<double>& guess) noexcept {
    const auto positive = element.positive_node >= 0 ? guess[static_cast<std::size_t>(element.positive_node)] : 0.0;
    const auto negative = element.negative_node >= 0 ? guess[static_cast<std::size_t>(element.negative_node)] : 0.0;
    const auto voltage = positive - negative;
    const auto evaluation = EvaluateNonlinearDevice(element.nonlinear_model, voltage);
    if (!evaluation.Succeeded()) {
        StampConductance(matrix, size, element.positive_node, element.negative_node,
                         std::numeric_limits<double>::quiet_NaN());
        return;
    }
    const auto equivalent_current = evaluation.current - evaluation.jacobian * voltage;
    StampConductance(matrix, size, element.positive_node, element.negative_node, evaluation.jacobian);
    StampCurrent(rhs, element.positive_node, element.negative_node, -equivalent_current);
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
        solution_.size() != matrix_size_ || newton_previous_.size() != matrix_size_ || max_iterations_ == 0U ||
        !std::isfinite(absolute_tolerance_) || !std::isfinite(relative_tolerance_) || absolute_tolerance_ <= 0.0 ||
        relative_tolerance_ <= 0.0) {
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
        if (element.inductor_history_index != kNoIndex) {
            inductor_history_[element.inductor_history_index] = element.initial;
        }
    }
    std::fill(matrix_.begin(), matrix_.end(), 0.0);
    std::fill(rhs_.begin(), rhs_.end(), 0.0);
    std::fill(solution_.begin(), solution_.end(), 0.0);
    std::fill(newton_previous_.begin(), newton_previous_.end(), 0.0);
    for (const auto& element : elements_) {
        if (element.type == CircuitComponentType::Diode && element.positive_node >= 0) {
            solution_[element.positive_node] = element.initial;
        }
        if (element.type == CircuitComponentType::Bjt && element.bjt_base_node >= 0) {
            solution_[element.bjt_base_node] = element.initial;
        }
        if (element.type == CircuitComponentType::Jfet && element.jfet_gate_node >= 0) {
            solution_[element.jfet_gate_node] = element.initial;
        }
        if (element.type == CircuitComponentType::Mosfet && element.mosfet_gate_node >= 0) {
            solution_[element.mosfet_gate_node] = element.initial;
        }
        if ((element.type == CircuitComponentType::OpAmp || element.type == CircuitComponentType::Ota) &&
            element.positive_node >= 0) {
            solution_[element.positive_node] = element.initial;
        }
        if (element.type == CircuitComponentType::Triode && element.triode_grid_node >= 0) {
            solution_[element.triode_grid_node] = element.initial;
        }
        if (element.type == CircuitComponentType::Pentode && element.pentode_grid_node >= 0) {
            solution_[element.pentode_grid_node] = element.initial;
        }
    }
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
        const auto has_nonlinearity = std::any_of(elements_.begin(), elements_.end(), [](const auto& element) {
            return element.type == CircuitComponentType::Diode || element.type == CircuitComponentType::Bjt ||
                   element.type == CircuitComponentType::Jfet || element.type == CircuitComponentType::Mosfet ||
                   element.type == CircuitComponentType::OpAmp || element.type == CircuitComponentType::Ota ||
                   element.type == CircuitComponentType::Triode || element.type == CircuitComponentType::Pentode ||
                   element.controlled_has_limits;
        });
        bool converged = !has_nonlinearity;
        for (std::uint32_t iteration = 0; iteration < max_iterations_; ++iteration) {
            std::copy(solution_.begin(), solution_.end(), newton_previous_.begin());
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
                case CircuitComponentType::Inductor: {
                    const double inductance_over_dt = element.value * conductance_scale;
                    StampInductor(matrix_, rhs_, matrix_size_, element.positive_node, element.negative_node,
                                  element.branch_index, inductance_over_dt,
                                  inductor_history_[element.inductor_history_index]);
                    break;
                }
                case CircuitComponentType::VoltageSource:
                    StampVoltageSource(matrix_, rhs_, matrix_size_, element.positive_node, element.negative_node,
                                       element.branch_index, element.value);
                    break;
                case CircuitComponentType::Diode:
                    StampDiode(matrix_, rhs_, matrix_size_, element, newton_previous_);
                    break;
                case CircuitComponentType::Bjt:
                    StampBjt(matrix_, rhs_, matrix_size_, element, newton_previous_);
                    break;
                case CircuitComponentType::Jfet:
                    StampJfet(matrix_, rhs_, matrix_size_, element, newton_previous_);
                    break;
                case CircuitComponentType::Mosfet:
                    StampMosfet(matrix_, rhs_, matrix_size_, element, newton_previous_);
                    break;
                case CircuitComponentType::Vccs:
                    StampVccs(matrix_, rhs_, matrix_size_, element, newton_previous_);
                    break;
                case CircuitComponentType::IdealOpAmp:
                    StampIdealOpAmp(matrix_, rhs_, matrix_size_, element, newton_previous_);
                    break;
                case CircuitComponentType::OpAmp:
                    StampOpAmp(matrix_, rhs_, matrix_size_, element, newton_previous_);
                    break;
                case CircuitComponentType::Ota:
                    StampOta(matrix_, rhs_, matrix_size_, element, newton_previous_);
                    break;
                case CircuitComponentType::Triode:
                    StampTriode(matrix_, rhs_, matrix_size_, element, newton_previous_);
                    break;
                case CircuitComponentType::Pentode:
                    StampPentode(matrix_, rhs_, matrix_size_, element, newton_previous_);
                    break;
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
            double maximum_delta = 0.0;
            double maximum_value = 1.0;
            double nonlinear_step_scale = 1.0;
            for (std::uint32_t index = 0; index < matrix_size_; ++index) {
                maximum_value = std::max(maximum_value, std::fabs(solution_[index]));
                maximum_delta = std::max(maximum_delta, std::fabs(solution_[index] - newton_previous_[index]));
            }
            for (const auto& element : elements_) {
                if (element.type == CircuitComponentType::Diode) {
                    const auto old_positive = element.positive_node >= 0 ? newton_previous_[element.positive_node] : 0.0;
                    const auto old_negative = element.negative_node >= 0 ? newton_previous_[element.negative_node] : 0.0;
                    const auto new_positive = element.positive_node >= 0 ? solution_[element.positive_node] : 0.0;
                    const auto new_negative = element.negative_node >= 0 ? solution_[element.negative_node] : 0.0;
                    const auto junction_step = std::fabs(
                        (new_positive - new_negative) - (old_positive - old_negative));
                    if (junction_step > element.nonlinear_model.max_step && std::isfinite(junction_step)) {
                        nonlinear_step_scale = std::min(
                            nonlinear_step_scale, element.nonlinear_model.max_step / junction_step);
                    }
                }
                if (element.type == CircuitComponentType::Bjt) {
                    const auto polarity = element.nonlinear_bjt_model.polarity == BjtPolarity::Npn ? 1.0 : -1.0;
                    const auto old_collector = element.positive_node >= 0 ? newton_previous_[element.positive_node] : 0.0;
                    const auto old_base = element.bjt_base_node >= 0 ? newton_previous_[element.bjt_base_node] : 0.0;
                    const auto old_emitter = element.negative_node >= 0 ? newton_previous_[element.negative_node] : 0.0;
                    const auto new_collector = element.positive_node >= 0 ? solution_[element.positive_node] : 0.0;
                    const auto new_base = element.bjt_base_node >= 0 ? solution_[element.bjt_base_node] : 0.0;
                    const auto new_emitter = element.negative_node >= 0 ? solution_[element.negative_node] : 0.0;
                    const auto old_be = polarity * (old_base - old_emitter);
                    const auto old_bc = polarity * (old_base - old_collector);
                    const auto new_be = polarity * (new_base - new_emitter);
                    const auto new_bc = polarity * (new_base - new_collector);
                    const auto be_delta = std::fabs(new_be - old_be);
                    const auto bc_delta = std::fabs(new_bc - old_bc);
                    if (be_delta > element.nonlinear_bjt_model.max_step ||
                        bc_delta > element.nonlinear_bjt_model.max_step) {
                        const auto largest_junction_step = std::max(be_delta, bc_delta);
                        if (largest_junction_step > 0.0 && std::isfinite(largest_junction_step)) {
                            nonlinear_step_scale = std::min(
                                nonlinear_step_scale, element.nonlinear_bjt_model.max_step / largest_junction_step);
                        }
                    }
                }
                if (element.type == CircuitComponentType::Jfet) {
                    const auto old_drain = element.positive_node >= 0 ? newton_previous_[element.positive_node] : 0.0;
                    const auto old_gate = element.jfet_gate_node >= 0 ? newton_previous_[element.jfet_gate_node] : 0.0;
                    const auto old_source = element.negative_node >= 0 ? newton_previous_[element.negative_node] : 0.0;
                    const auto new_drain = element.positive_node >= 0 ? solution_[element.positive_node] : 0.0;
                    const auto new_gate = element.jfet_gate_node >= 0 ? solution_[element.jfet_gate_node] : 0.0;
                    const auto new_source = element.negative_node >= 0 ? solution_[element.negative_node] : 0.0;
                    const auto gate_source_delta = std::fabs(
                        (new_gate - new_source) - (old_gate - old_source));
                    const auto drain_source_delta = std::fabs(
                        (new_drain - new_source) - (old_drain - old_source));
                    const auto largest_step = std::max(gate_source_delta, drain_source_delta);
                    if (largest_step > element.nonlinear_jfet_model.max_step && std::isfinite(largest_step)) {
                        nonlinear_step_scale = std::min(
                            nonlinear_step_scale, element.nonlinear_jfet_model.max_step / largest_step);
                    }
                }
                if (element.type == CircuitComponentType::Mosfet) {
                    const auto old_drain = element.positive_node >= 0 ? newton_previous_[element.positive_node] : 0.0;
                    const auto old_gate = element.mosfet_gate_node >= 0 ? newton_previous_[element.mosfet_gate_node] : 0.0;
                    const auto old_source = element.negative_node >= 0 ? newton_previous_[element.negative_node] : 0.0;
                    const auto new_drain = element.positive_node >= 0 ? solution_[element.positive_node] : 0.0;
                    const auto new_gate = element.mosfet_gate_node >= 0 ? solution_[element.mosfet_gate_node] : 0.0;
                    const auto new_source = element.negative_node >= 0 ? solution_[element.negative_node] : 0.0;
                    const auto gate_source_delta = std::fabs(
                        (new_gate - new_source) - (old_gate - old_source));
                    const auto drain_source_delta = std::fabs(
                        (new_drain - new_source) - (old_drain - old_source));
                    const auto largest_step = std::max(gate_source_delta, drain_source_delta);
                    if (largest_step > element.nonlinear_mosfet_model.max_step && std::isfinite(largest_step)) {
                        nonlinear_step_scale = std::min(
                            nonlinear_step_scale, element.nonlinear_mosfet_model.max_step / largest_step);
                    }
                }
                if (element.type == CircuitComponentType::OpAmp || element.type == CircuitComponentType::Ota) {
                    const auto old_positive = element.control_positive_node >= 0
                        ? newton_previous_[element.control_positive_node] : 0.0;
                    const auto old_negative = element.control_negative_node >= 0
                        ? newton_previous_[element.control_negative_node] : 0.0;
                    const auto new_positive = element.control_positive_node >= 0
                        ? solution_[element.control_positive_node] : 0.0;
                    const auto new_negative = element.control_negative_node >= 0
                        ? solution_[element.control_negative_node] : 0.0;
                    const auto control_step = std::fabs(
                        (new_positive - new_negative) - (old_positive - old_negative));
                    if (control_step > element.nonlinear_amplifier_model.max_step &&
                        std::isfinite(control_step)) {
                        nonlinear_step_scale = std::min(
                            nonlinear_step_scale, element.nonlinear_amplifier_model.max_step / control_step);
                    }
                }
                if (element.type == CircuitComponentType::Triode) {
                    const auto old_plate = element.positive_node >= 0 ? newton_previous_[element.positive_node] : 0.0;
                    const auto old_grid = element.triode_grid_node >= 0 ? newton_previous_[element.triode_grid_node] : 0.0;
                    const auto old_cathode = element.negative_node >= 0 ? newton_previous_[element.negative_node] : 0.0;
                    const auto new_plate = element.positive_node >= 0 ? solution_[element.positive_node] : 0.0;
                    const auto new_grid = element.triode_grid_node >= 0 ? solution_[element.triode_grid_node] : 0.0;
                    const auto new_cathode = element.negative_node >= 0 ? solution_[element.negative_node] : 0.0;
                    const auto grid_cathode_delta = std::fabs(
                        (new_grid - new_cathode) - (old_grid - old_cathode));
                    const auto plate_cathode_delta = std::fabs(
                        (new_plate - new_cathode) - (old_plate - old_cathode));
                    const auto largest_step = std::max(grid_cathode_delta, plate_cathode_delta);
                    if (largest_step > element.nonlinear_triode_model.max_step && std::isfinite(largest_step)) {
                        nonlinear_step_scale = std::min(
                            nonlinear_step_scale, element.nonlinear_triode_model.max_step / largest_step);
                    }
                }
                if (element.type == CircuitComponentType::Pentode) {
                    const auto old_p = element.positive_node >= 0 ? newton_previous_[element.positive_node] : 0.0;
                    const auto old_s = element.pentode_screen_node >= 0 ? newton_previous_[element.pentode_screen_node] : 0.0;
                    const auto old_g = element.pentode_grid_node >= 0 ? newton_previous_[element.pentode_grid_node] : 0.0;
                    const auto old_k = element.negative_node >= 0 ? newton_previous_[element.negative_node] : 0.0;
                    const auto new_p = element.positive_node >= 0 ? solution_[element.positive_node] : 0.0;
                    const auto new_s = element.pentode_screen_node >= 0 ? solution_[element.pentode_screen_node] : 0.0;
                    const auto new_g = element.pentode_grid_node >= 0 ? solution_[element.pentode_grid_node] : 0.0;
                    const auto new_k = element.negative_node >= 0 ? solution_[element.negative_node] : 0.0;
                    const double largest = std::max({std::fabs((new_p-new_k)-(old_p-old_k)),
                        std::fabs((new_s-new_k)-(old_s-old_k)), std::fabs((new_g-new_k)-(old_g-old_k))});
                    if (largest > element.nonlinear_pentode_model.max_step && std::isfinite(largest)) {
                        nonlinear_step_scale = std::min(nonlinear_step_scale,
                            element.nonlinear_pentode_model.max_step / largest);
                    }
                }
            }
            // Nonlinear junction steps are damped, rather than allocating a second
            // trial vector. If the bounded iteration budget is exhausted the
            // caller receives NonConvergent deterministically.
            if (nonlinear_step_scale < 1.0) {
                for (std::uint32_t index = 0; index < matrix_size_; ++index) {
                    solution_[index] = newton_previous_[index] +
                        nonlinear_step_scale * (solution_[index] - newton_previous_[index]);
                }
                maximum_delta = 0.0;
                maximum_value = 1.0;
                for (std::uint32_t index = 0; index < matrix_size_; ++index) {
                    maximum_value = std::max(maximum_value, std::fabs(solution_[index]));
                    maximum_delta = std::max(maximum_delta, std::fabs(solution_[index] - newton_previous_[index]));
                }
            }
            // A limited junction step is progress, not convergence. Otherwise
            // maxStep values below the numeric tolerance could falsely accept
            // the initial guess without satisfying the undamped Newton system.
            if (nonlinear_step_scale >= 1.0 &&
                (!has_nonlinearity || maximum_delta <= absolute_tolerance_ + relative_tolerance_ * maximum_value)) {
                converged = true;
                break;
            }
        }
        if (!converged) {
            return {.error = MnaSolveError::NonConvergent};
        }
        for (const auto& element : elements_) {
            if (element.history_index != kNoIndex) {
                const double positive = element.positive_node >= 0 ? solution_[element.positive_node] : 0.0;
                const double negative = element.negative_node >= 0 ? solution_[element.negative_node] : 0.0;
                capacitor_history_[element.history_index] = positive - negative;
            }
            if (element.inductor_history_index != kNoIndex) {
                inductor_history_[element.inductor_history_index] = solution_[element.branch_index];
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
    if (circuit.solver.convergence.max_iterations == 0U || circuit.solver.convergence.max_iterations > 1024U ||
        !std::isfinite(circuit.solver.convergence.absolute_tolerance) ||
        !std::isfinite(circuit.solver.convergence.relative_tolerance) ||
        circuit.solver.convergence.absolute_tolerance <= 0.0 || circuit.solver.convergence.relative_tolerance <= 0.0) {
        return {.circuit = std::nullopt, .error = MnaCompileError::InvalidConvergence};
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
        if (component.initial.has_value() && !std::isfinite(*component.initial)) {
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
        }
        if ((component.bjt_model.has_value() && component.type != CircuitComponentType::Bjt) ||
            (component.jfet_model.has_value() && component.type != CircuitComponentType::Jfet) ||
            (component.mosfet_model.has_value() && component.type != CircuitComponentType::Mosfet) ||
            (component.amplifier_model.has_value() && component.type != CircuitComponentType::OpAmp &&
             component.type != CircuitComponentType::Ota) ||
            (component.triode_model.has_value() && component.type != CircuitComponentType::Triode) ||
            (component.pentode_model.has_value() && component.type != CircuitComponentType::Pentode)) {
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
        }
        if (component.type != CircuitComponentType::Resistor && component.type != CircuitComponentType::Capacitor &&
            component.type != CircuitComponentType::Inductor &&
            component.type != CircuitComponentType::VoltageSource && component.type != CircuitComponentType::Diode &&
            component.type != CircuitComponentType::Potentiometer && component.type != CircuitComponentType::Vccs &&
            component.type != CircuitComponentType::IdealOpAmp && component.type != CircuitComponentType::OpAmp &&
            component.type != CircuitComponentType::Ota && component.type != CircuitComponentType::Bjt &&
            component.type != CircuitComponentType::Jfet && component.type != CircuitComponentType::Mosfet &&
            component.type != CircuitComponentType::Triode && component.type != CircuitComponentType::Pentode) {
            return {.circuit = std::nullopt, .error = MnaCompileError::UnsupportedComponent};
        }
        if (component.type == CircuitComponentType::Bjt) {
            if (component.pins.size() != 3U || (!component.bjt_model.has_value() && !component.model.has_value()) || component.value.has_value() ||
                component.tolerance.has_value() || component.diode_model.has_value() ||
                component.potentiometer.has_value() || component.controlled_source.has_value() ||
                component.jfet_model.has_value() || component.mosfet_model.has_value() ||
                component.switch_spec.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            if (component.model.has_value() && !component.bjt_model.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            const auto& bjt = *component.bjt_model;
            if (bjt.kind != "ebers-moll" || bjt.model_version != 1U ||
                (bjt.polarity != "npn" && bjt.polarity != "pnp")) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            if (!std::isfinite(bjt.saturation_current) || !std::isfinite(bjt.forward_beta) ||
                !std::isfinite(bjt.reverse_beta) || !std::isfinite(bjt.thermal_voltage) ||
                !std::isfinite(bjt.max_current) || !std::isfinite(bjt.max_step) ||
                bjt.saturation_current <= 0.0 || bjt.saturation_current > 1.0 || bjt.forward_beta <= 0.0 ||
                bjt.forward_beta > 1.0e6 || bjt.reverse_beta <= 0.0 || bjt.reverse_beta > 1.0e6 ||
                bjt.thermal_voltage <= 0.0 || bjt.thermal_voltage > 1.0 || bjt.max_current <= 0.0 ||
                bjt.max_current > 1.0e6 || bjt.max_step <= 0.0 || bjt.max_step > 10.0) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
            }
        }
        if (component.type == CircuitComponentType::Jfet) {
            if (component.pins.size() != 3U || (!component.jfet_model.has_value() && !component.model.has_value()) ||
                component.value.has_value() || component.tolerance.has_value() || component.diode_model.has_value() ||
                component.potentiometer.has_value() || component.controlled_source.has_value() ||
                component.bjt_model.has_value() || component.mosfet_model.has_value() ||
                component.switch_spec.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            if (component.model.has_value() && !component.jfet_model.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            const auto& jfet = *component.jfet_model;
            if (jfet.kind != "square-law" || jfet.model_version != 1U ||
                (jfet.polarity != "n-channel" && jfet.polarity != "p-channel")) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            const NonlinearJfetModel model{
                .polarity = jfet.polarity == "n-channel" ? JfetPolarity::NChannel : JfetPolarity::PChannel,
                .pinch_off_voltage = jfet.pinch_off_voltage,
                .transconductance_parameter = jfet.transconductance_parameter,
                .max_current = jfet.max_current,
                .max_step = jfet.max_step};
            if (!ValidateNonlinearJfetModel(model)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
            }
        }
        if (component.type == CircuitComponentType::Mosfet) {
            if (component.pins.size() != 3U || (!component.mosfet_model.has_value() && !component.model.has_value()) ||
                component.value.has_value() || component.tolerance.has_value() || component.diode_model.has_value() ||
                component.potentiometer.has_value() || component.controlled_source.has_value() ||
                component.bjt_model.has_value() || component.jfet_model.has_value() ||
                component.switch_spec.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            if (component.model.has_value() && !component.mosfet_model.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            const auto& mosfet = *component.mosfet_model;
            if (mosfet.kind != "enhancement-square-law" || mosfet.model_version != 1U ||
                (mosfet.polarity != "n-channel" && mosfet.polarity != "p-channel")) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            const NonlinearMosfetModel model{
                .polarity = mosfet.polarity == "n-channel" ? MosfetPolarity::NChannel : MosfetPolarity::PChannel,
                .threshold_voltage = mosfet.threshold_voltage,
                .transconductance_parameter = mosfet.transconductance_parameter,
                .max_current = mosfet.max_current,
                .max_step = mosfet.max_step};
            if (!ValidateNonlinearMosfetModel(model)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
            }
        }
        if (component.type == CircuitComponentType::OpAmp || component.type == CircuitComponentType::Ota) {
            if (component.pins.size() != 4U ||
                (!component.amplifier_model.has_value() && !component.model.has_value()) ||
                component.value.has_value() || component.tolerance.has_value() ||
                component.diode_model.has_value() || component.potentiometer.has_value() ||
                component.controlled_source.has_value() || component.bjt_model.has_value() ||
                component.jfet_model.has_value() || component.mosfet_model.has_value() ||
                component.switch_spec.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            if (component.model.has_value() && !component.amplifier_model.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            const auto& amplifier = *component.amplifier_model;
            const auto expected_kind = component.type == CircuitComponentType::OpAmp
                ? "bounded-opamp" : "bounded-ota";
            if (amplifier.kind != expected_kind || amplifier.model_version != 1U) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            const NonlinearAmplifierModel model{
                .kind = component.type == CircuitComponentType::OpAmp
                    ? AmplifierKind::OpAmp : AmplifierKind::Ota,
                .gain = amplifier.gain,
                .minimum_output = amplifier.minimum_output,
                .maximum_output = amplifier.maximum_output,
                .max_step = amplifier.max_step};
            if (!ValidateNonlinearAmplifierModel(model)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
            }
        }
        if (component.type == CircuitComponentType::Triode) {
            if (component.pins.size() != 3U ||
                (!component.triode_model.has_value() && !component.model.has_value()) ||
                component.value.has_value() || component.tolerance.has_value() ||
                component.diode_model.has_value() || component.potentiometer.has_value() ||
                component.controlled_source.has_value() || component.bjt_model.has_value() ||
                component.jfet_model.has_value() || component.mosfet_model.has_value() ||
                component.amplifier_model.has_value() || component.switch_spec.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            if (component.model.has_value() && !component.triode_model.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            const auto& triode = *component.triode_model;
            if (triode.kind != "space-charge" || triode.model_version != 1U) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            const NonlinearTriodeModel model{
                .amplification_factor = triode.amplification_factor,
                .perveance = triode.perveance,
                .max_current = triode.max_current,
                .max_step = triode.max_step};
            if (!ValidateNonlinearTriodeModel(model)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
            }
        }
        if (component.type == CircuitComponentType::Pentode) {
            if (component.pins.size() != 4U || (!component.pentode_model.has_value() && !component.model.has_value()) ||
                component.value.has_value() || component.tolerance.has_value() || component.diode_model.has_value() ||
                component.potentiometer.has_value() || component.controlled_source.has_value() ||
                component.bjt_model.has_value() || component.jfet_model.has_value() || component.mosfet_model.has_value() ||
                component.amplifier_model.has_value() || component.triode_model.has_value() || component.switch_spec.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            if (component.model.has_value() && !component.pentode_model.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            const auto& item = *component.pentode_model;
            const NonlinearPentodeModel model{item.screen_amplification_factor, item.perveance, item.knee_voltage,
                item.screen_current_fraction, item.max_current, item.max_step};
            if (item.kind != "space-charge" || item.model_version != 1U || !ValidateNonlinearPentodeModel(model)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
            }
        }
        if ((component.type == CircuitComponentType::Resistor || component.type == CircuitComponentType::Capacitor ||
             component.type == CircuitComponentType::Inductor ||
             component.type == CircuitComponentType::VoltageSource || component.type == CircuitComponentType::Potentiometer) &&
            (!component.value.has_value() || !std::isfinite(*component.value) ||
            ((component.type == CircuitComponentType::Resistor || component.type == CircuitComponentType::Capacitor ||
              component.type == CircuitComponentType::Inductor || component.type == CircuitComponentType::Potentiometer) &&
              *component.value <= 0.0))) {
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
        }
        if (component.type == CircuitComponentType::Potentiometer) {
            if (!component.potentiometer.has_value() || component.model.has_value() ||
                !std::isfinite(component.potentiometer->position) ||
                !std::isfinite(component.potentiometer->minimum) ||
                !std::isfinite(component.potentiometer->maximum) ||
                component.potentiometer->minimum >= component.potentiometer->maximum ||
                component.potentiometer->position < component.potentiometer->minimum ||
                component.potentiometer->position > component.potentiometer->maximum) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
            }
        }
        if (component.type == CircuitComponentType::Vccs || component.type == CircuitComponentType::IdealOpAmp) {
            if (!component.controlled_source.has_value() || component.pins.size() != 4U || component.model.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            const auto& controlled = *component.controlled_source;
            const auto expected_kind = component.type == CircuitComponentType::Vccs ? "vccs" : "ideal-op-amp";
            if (controlled.kind != expected_kind || controlled.model_version != 1U ||
                !std::isfinite(controlled.gain) || std::fabs(controlled.gain) > 1.0e12 ||
                (component.type == CircuitComponentType::IdealOpAmp && controlled.gain <= 0.0) ||
                controlled.minimum.has_value() != controlled.maximum.has_value() ||
                (controlled.minimum.has_value() &&
                 (!std::isfinite(*controlled.minimum) || !std::isfinite(*controlled.maximum) ||
                  *controlled.minimum > *controlled.maximum))) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
            }
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
    const auto inductor_count = static_cast<std::uint32_t>(std::count_if(
        circuit.components.begin(), circuit.components.end(), [](const CircuitComponent& component) {
            return component.type == CircuitComponentType::Inductor;
        }));
    const auto controlled_voltage_source_count = static_cast<std::uint32_t>(std::count_if(
        circuit.components.begin(), circuit.components.end(), [](const CircuitComponent& component) {
            return component.type == CircuitComponentType::IdealOpAmp ||
                   component.type == CircuitComponentType::OpAmp;
        }));
    const auto branch_count = voltage_source_count + inductor_count + controlled_voltage_source_count;
    if (non_ground_nodes + branch_count == 0U) {
        return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
    }
    prepared.matrix_size_ = non_ground_nodes + branch_count;
    prepared.matrix_.assign(static_cast<std::size_t>(prepared.matrix_size_) * prepared.matrix_size_, 0.0);
    prepared.rhs_.assign(prepared.matrix_size_, 0.0);
    prepared.solution_.assign(prepared.matrix_size_, 0.0);
    prepared.newton_previous_.assign(prepared.matrix_size_, 0.0);
    prepared.oversampling_ = circuit.solver.oversampling;
    prepared.max_iterations_ = circuit.solver.convergence.max_iterations;
    prepared.absolute_tolerance_ = circuit.solver.convergence.absolute_tolerance;
    prepared.relative_tolerance_ = circuit.solver.convergence.relative_tolerance;
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
    constexpr double kMinimumPotSegmentResistance = 1.0e-12;
    for (const auto& component : circuit.components) {
        const auto positive = FindNode(circuit, component.pins[0]);
        const bool is_three_terminal = component.type == CircuitComponentType::Bjt ||
            component.type == CircuitComponentType::Jfet || component.type == CircuitComponentType::Mosfet ||
            component.type == CircuitComponentType::Triode;
        const auto negative = FindNode(circuit, component.pins[
            is_three_terminal ? 2U : (component.type == CircuitComponentType::Pentode ? 3U : 1U)]);
        if (positive == -2 || negative == -2 || positive == negative) {
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
        }
        if (component.type == CircuitComponentType::Bjt) {
            const auto base = FindNode(circuit, component.pins[1]);
            if (base == -2 || base == positive || base == negative) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
            }
            const auto& bjt = *component.bjt_model;
            MnaPreparedCircuit::Element element{.type = CircuitComponentType::Bjt,
                                                .positive_node = positive,
                                                .negative_node = negative,
                                                .control_positive_node = -1,
                                                .control_negative_node = -1,
                                                .bjt_base_node = base,
                                                .branch_index = kNoIndex,
                                                .history_index = kNoIndex,
                                                .inductor_history_index = kNoIndex,
                                                .value = 0.0,
                                                .initial = component.initial.value_or(0.0),
                                                .nonlinear_model = {},
                                                .controlled_gain = 0.0,
                                                .controlled_minimum = 0.0,
                                                .controlled_maximum = 0.0,
                                                .controlled_has_limits = false,
                                                .nonlinear_bjt_model = {
                                                    .polarity = bjt.polarity == "npn" ? BjtPolarity::Npn : BjtPolarity::Pnp,
                                                    .saturation_current = bjt.saturation_current,
                                                    .forward_beta = bjt.forward_beta,
                                                    .reverse_beta = bjt.reverse_beta,
                                                    .thermal_voltage = bjt.thermal_voltage,
                                                    .max_current = bjt.max_current,
                                                    .max_step = bjt.max_step}};
            if (!ValidateNonlinearBjtModel(element.nonlinear_bjt_model)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            prepared.elements_.push_back(element);
            continue;
        }
        if (component.type == CircuitComponentType::Jfet) {
            const auto gate = FindNode(circuit, component.pins[1]);
            if (gate == -2 || gate == positive || gate == negative) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
            }
            const auto& jfet = *component.jfet_model;
            MnaPreparedCircuit::Element element{.type = CircuitComponentType::Jfet,
                                                .positive_node = positive,
                                                .negative_node = negative,
                                                .control_positive_node = -1,
                                                .control_negative_node = -1,
                                                .bjt_base_node = -1,
                                                .jfet_gate_node = gate,
                                                .branch_index = kNoIndex,
                                                .history_index = kNoIndex,
                                                .inductor_history_index = kNoIndex,
                                                .value = 0.0,
                                                .initial = component.initial.value_or(0.0),
                                                .nonlinear_model = {},
                                                .controlled_gain = 0.0,
                                                .controlled_minimum = 0.0,
                                                .controlled_maximum = 0.0,
                                                .controlled_has_limits = false,
                                                .nonlinear_bjt_model = {},
                                                .nonlinear_jfet_model = {
                                                    .polarity = jfet.polarity == "n-channel"
                                                        ? JfetPolarity::NChannel : JfetPolarity::PChannel,
                                                    .pinch_off_voltage = jfet.pinch_off_voltage,
                                                    .transconductance_parameter = jfet.transconductance_parameter,
                                                    .max_current = jfet.max_current,
                                                    .max_step = jfet.max_step}};
            if (!ValidateNonlinearJfetModel(element.nonlinear_jfet_model)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            prepared.elements_.push_back(element);
            continue;
        }
        if (component.type == CircuitComponentType::Mosfet) {
            const auto gate = FindNode(circuit, component.pins[1]);
            if (gate == -2 || gate == positive || gate == negative) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
            }
            const auto& mosfet = *component.mosfet_model;
            MnaPreparedCircuit::Element element{.type = CircuitComponentType::Mosfet,
                                                .positive_node = positive,
                                                .negative_node = negative,
                                                .control_positive_node = -1,
                                                .control_negative_node = -1,
                                                .bjt_base_node = -1,
                                                .jfet_gate_node = -1,
                                                .mosfet_gate_node = gate,
                                                .branch_index = kNoIndex,
                                                .history_index = kNoIndex,
                                                .inductor_history_index = kNoIndex,
                                                .value = 0.0,
                                                .initial = component.initial.value_or(0.0),
                                                .nonlinear_model = {},
                                                .controlled_gain = 0.0,
                                                .controlled_minimum = 0.0,
                                                .controlled_maximum = 0.0,
                                                .controlled_has_limits = false,
                                                .nonlinear_bjt_model = {},
                                                .nonlinear_jfet_model = {},
                                                .nonlinear_mosfet_model = {
                                                    .polarity = mosfet.polarity == "n-channel"
                                                        ? MosfetPolarity::NChannel : MosfetPolarity::PChannel,
                                                    .threshold_voltage = mosfet.threshold_voltage,
                                                    .transconductance_parameter = mosfet.transconductance_parameter,
                                                    .max_current = mosfet.max_current,
                                                    .max_step = mosfet.max_step}};
            if (!ValidateNonlinearMosfetModel(element.nonlinear_mosfet_model)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            prepared.elements_.push_back(element);
            continue;
        }
        if (component.type == CircuitComponentType::Triode) {
            const auto grid = FindNode(circuit, component.pins[1]);
            if (grid == -2 || grid == positive || grid == negative) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
            }
            const auto& triode = *component.triode_model;
            MnaPreparedCircuit::Element element{};
            element.type = CircuitComponentType::Triode;
            element.positive_node = positive;
            element.negative_node = negative;
            element.triode_grid_node = grid;
            element.branch_index = kNoIndex;
            element.initial = component.initial.value_or(0.0);
            element.nonlinear_triode_model = {
                .amplification_factor = triode.amplification_factor,
                .perveance = triode.perveance,
                .max_current = triode.max_current,
                .max_step = triode.max_step};
            if (!ValidateNonlinearTriodeModel(element.nonlinear_triode_model)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            prepared.elements_.push_back(element);
            continue;
        }
        if (component.type == CircuitComponentType::Pentode) {
            const auto screen = FindNode(circuit, component.pins[1]);
            const auto grid = FindNode(circuit, component.pins[2]);
            if (screen == -2 || grid == -2 || screen == positive || screen == negative ||
                grid == positive || grid == screen || grid == negative) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
            }
            const auto& item = *component.pentode_model;
            MnaPreparedCircuit::Element element{};
            element.type = CircuitComponentType::Pentode;
            element.positive_node = positive;
            element.negative_node = negative;
            element.pentode_screen_node = screen;
            element.pentode_grid_node = grid;
            element.branch_index = kNoIndex;
            element.initial = component.initial.value_or(0.0);
            element.nonlinear_pentode_model = {item.screen_amplification_factor, item.perveance,
                item.knee_voltage, item.screen_current_fraction, item.max_current, item.max_step};
            prepared.elements_.push_back(element);
            continue;
        }
        if (component.type == CircuitComponentType::Potentiometer) {
            const auto& spec = *component.potentiometer;
            std::string wiper_name;
            if (spec.wiper_node.has_value()) {
                wiper_name = *spec.wiper_node;
            } else if (component.pins.size() == 3U) {
                wiper_name = component.pins[2];
            } else {
                return {.circuit = std::nullopt, .error = MnaCompileError::UnsupportedComponent};
            }
            const auto wiper = FindNode(circuit, wiper_name);
            if (wiper < 0 || wiper == positive || wiper == negative) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
            }
            const auto normalized = (spec.position - spec.minimum) / (spec.maximum - spec.minimum);
            const auto total = *component.value;
            const auto first_value = std::max(total * normalized, kMinimumPotSegmentResistance);
            const auto second_value = std::max(total * (1.0 - normalized), kMinimumPotSegmentResistance);
            if (!std::isfinite(first_value) || !std::isfinite(second_value)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
            }
            prepared.elements_.push_back({.type = CircuitComponentType::Resistor,
                                          .positive_node = positive,
                                          .negative_node = wiper,
                                          .branch_index = kNoIndex,
                                          .history_index = kNoIndex,
                                          .inductor_history_index = kNoIndex,
                                          .value = first_value,
                                          .initial = 0.0});
            prepared.elements_.push_back({.type = CircuitComponentType::Resistor,
                                          .positive_node = wiper,
                                          .negative_node = negative,
                                          .branch_index = kNoIndex,
                                          .history_index = kNoIndex,
                                          .inductor_history_index = kNoIndex,
                                          .value = second_value,
                                          .initial = 0.0});
            continue;
        }
        if (component.type == CircuitComponentType::OpAmp || component.type == CircuitComponentType::Ota) {
            const auto control_positive = FindNode(circuit, component.pins[2]);
            const auto control_negative = FindNode(circuit, component.pins[3]);
            if (control_positive == -2 || control_negative == -2) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
            }
            const auto& amplifier = *component.amplifier_model;
            MnaPreparedCircuit::Element element{};
            element.type = component.type;
            element.positive_node = positive;
            element.negative_node = negative;
            element.control_positive_node = control_positive;
            element.control_negative_node = control_negative;
            element.branch_index = component.type == CircuitComponentType::OpAmp
                ? non_ground_nodes + branch++ : kNoIndex;
            element.initial = component.initial.value_or(0.0);
            element.nonlinear_amplifier_model = {
                .kind = component.type == CircuitComponentType::OpAmp
                    ? AmplifierKind::OpAmp : AmplifierKind::Ota,
                .gain = amplifier.gain,
                .minimum_output = amplifier.minimum_output,
                .maximum_output = amplifier.maximum_output,
                .max_step = amplifier.max_step};
            if (!ValidateNonlinearAmplifierModel(element.nonlinear_amplifier_model)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            prepared.elements_.push_back(element);
            continue;
        }
        if (component.type == CircuitComponentType::Vccs || component.type == CircuitComponentType::IdealOpAmp) {
            const auto control_positive = FindNode(circuit, component.pins[2]);
            const auto control_negative = FindNode(circuit, component.pins[3]);
            if (control_positive == -2 || control_negative == -2) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
            }
            const auto& controlled = *component.controlled_source;
            MnaPreparedCircuit::Element element{.type = component.type,
                                                .positive_node = positive,
                                                .negative_node = negative,
                                                .control_positive_node = control_positive,
                                                .control_negative_node = control_negative,
                                                .branch_index = kNoIndex,
                                                .history_index = kNoIndex,
                                                .inductor_history_index = kNoIndex,
                                                .value = 0.0,
                                                .initial = 0.0,
                                                .nonlinear_model = {},
                                                .controlled_gain = controlled.gain,
                                                .controlled_minimum = controlled.minimum.value_or(0.0),
                                                .controlled_maximum = controlled.maximum.value_or(0.0),
                                                .controlled_has_limits = controlled.minimum.has_value()};
            if (component.type == CircuitComponentType::IdealOpAmp) {
                element.branch_index = non_ground_nodes + branch++;
            }
            prepared.elements_.push_back(element);
            continue;
        }
        MnaPreparedCircuit::Element element{.type = component.type,
                                            .positive_node = positive,
                                            .negative_node = negative,
                                            .control_positive_node = -1,
                                            .control_negative_node = -1,
                                            .branch_index = kNoIndex,
                                            .history_index = kNoIndex,
                                            .inductor_history_index = kNoIndex,
                                            .value = component.value.value_or(0.0),
                                            .initial = component.initial.value_or(0.0),
                                            .nonlinear_model = {}};
        if (component.type == CircuitComponentType::VoltageSource) {
            element.branch_index = non_ground_nodes + branch++;
        }
        if (component.type == CircuitComponentType::Inductor) {
            element.branch_index = non_ground_nodes + branch++;
            element.inductor_history_index = static_cast<std::uint32_t>(prepared.inductor_history_.size());
            prepared.inductor_history_.push_back(0.0);
        }
        if (component.type == CircuitComponentType::Capacitor) {
            element.history_index = history++;
        }
        if (component.type == CircuitComponentType::Diode) {
            if (!component.diode_model.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            if (component.diode_model->model_version != 1U) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            element.nonlinear_model = {
                .kind = component.diode_model->kind == "shockley" ? NonlinearDeviceKind::Diode
                                                                  : NonlinearDeviceKind::Zener,
                .saturation_current = component.diode_model->saturation_current,
                .thermal_voltage = component.diode_model->thermal_voltage,
                .breakdown_voltage = component.diode_model->breakdown_voltage,
                .breakdown_current = component.diode_model->breakdown_current,
                .breakdown_slope = component.diode_model->breakdown_slope,
                .max_step = component.diode_model->max_step};
            if (!ValidateNonlinearDeviceModel(element.nonlinear_model)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
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
                                      .control_positive_node = -1,
                                      .control_negative_node = -1,
                                      .branch_index = non_ground_nodes + branch++,
                                      .history_index = kNoIndex,
                                      .inductor_history_index = kNoIndex,
                                      .value = supply.voltage,
                                      .initial = 0.0});
    }
    if (input_port != circuit.ports.end()) {
        prepared.input_branch_ = non_ground_nodes + branch++;
    }
    if (branch != branch_count) {
        return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
    }
    prepared.capacitor_history_.assign(history, 0.0);
    return {.circuit = std::move(prepared), .error = std::nullopt};
}

MnaCompileResult CompileMnaCircuit(const CircuitNetlist& circuit, const ModelDatabase& model_database) {
    CircuitNetlist bound = circuit;
    for (auto& component : bound.components) {
        if (component.type != CircuitComponentType::Diode && component.type != CircuitComponentType::Bjt &&
            component.type != CircuitComponentType::Jfet && component.type != CircuitComponentType::Mosfet &&
            component.type != CircuitComponentType::OpAmp && component.type != CircuitComponentType::Ota &&
            component.type != CircuitComponentType::Triode && component.type != CircuitComponentType::Pentode) {
            continue;
        }
        if (!component.model.has_value()) {
            if (component.type == CircuitComponentType::Bjt && component.bjt_model.has_value()) {
                continue;
            }
            if (component.type == CircuitComponentType::Jfet && component.jfet_model.has_value()) {
                continue;
            }
            if (component.type == CircuitComponentType::Mosfet && component.mosfet_model.has_value()) {
                continue;
            }
            if ((component.type == CircuitComponentType::OpAmp || component.type == CircuitComponentType::Ota) &&
                component.amplifier_model.has_value()) {
                continue;
            }
            if (component.type == CircuitComponentType::Triode && component.triode_model.has_value()) {
                continue;
            }
            if (component.type == CircuitComponentType::Pentode && component.pentode_model.has_value()) continue;
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
        }
        if (component.type == CircuitComponentType::Bjt) {
            const auto result = model_database.Bind({*component.model, ModelKind::Bjt});
            if (!result.model.has_value() || !result.model->polarity.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            const auto& record = *result.model;
            const auto saturation = ModelParameter(record, "saturation-current");
            const auto forward_beta = ModelParameter(record, "forward-beta");
            const auto reverse_beta = ModelParameter(record, "reverse-beta");
            const auto thermal = ModelParameter(record, "thermal-voltage");
            const auto max_current = ModelParameter(record, "max-current");
            const auto max_step = ModelParameter(record, "max-step");
            if (!saturation.has_value() || !forward_beta.has_value() || !reverse_beta.has_value() ||
                !thermal.has_value() || !max_current.has_value() || !max_step.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            component.bjt_model = CircuitBjtModel{
                .kind = "ebers-moll", .model_version = 1U, .polarity = *record.polarity,
                .saturation_current = *saturation, .forward_beta = *forward_beta,
                .reverse_beta = *reverse_beta, .thermal_voltage = *thermal,
                .max_current = *max_current, .max_step = *max_step};
            continue;
        }
        if (component.type == CircuitComponentType::Jfet) {
            const auto result = model_database.Bind({*component.model, ModelKind::Jfet});
            if (!result.model.has_value() || !result.model->polarity.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            const auto& record = *result.model;
            const auto pinch_off = ModelParameter(record, "pinch-off-voltage");
            const auto transconductance = ModelParameter(record, "transconductance-parameter");
            const auto max_current = ModelParameter(record, "max-current");
            const auto max_step = ModelParameter(record, "max-step");
            if (!pinch_off.has_value() || !transconductance.has_value() ||
                !max_current.has_value() || !max_step.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            component.jfet_model = CircuitJfetModel{
                .kind = "square-law", .model_version = 1U, .polarity = *record.polarity,
                .pinch_off_voltage = *pinch_off, .transconductance_parameter = *transconductance,
                .max_current = *max_current, .max_step = *max_step};
            continue;
        }
        if (component.type == CircuitComponentType::Mosfet) {
            const auto result = model_database.Bind({*component.model, ModelKind::Mosfet});
            if (!result.model.has_value() || !result.model->polarity.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            const auto& record = *result.model;
            const auto threshold = ModelParameter(record, "threshold-voltage");
            const auto transconductance = ModelParameter(record, "transconductance-parameter");
            const auto max_current = ModelParameter(record, "max-current");
            const auto max_step = ModelParameter(record, "max-step");
            if (!threshold.has_value() || !transconductance.has_value() ||
                !max_current.has_value() || !max_step.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            component.mosfet_model = CircuitMosfetModel{
                .kind = "enhancement-square-law", .model_version = 1U, .polarity = *record.polarity,
                .threshold_voltage = *threshold, .transconductance_parameter = *transconductance,
                .max_current = *max_current, .max_step = *max_step};
            continue;
        }
        if (component.type == CircuitComponentType::OpAmp || component.type == CircuitComponentType::Ota) {
            const auto expected_kind = component.type == CircuitComponentType::OpAmp
                ? ModelKind::Opamp : ModelKind::Ota;
            const auto result = model_database.Bind({*component.model, expected_kind});
            if (!result.model.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            const auto& record = *result.model;
            const auto gain = ModelParameter(
                record, component.type == CircuitComponentType::OpAmp ? "open-loop-gain" : "transconductance");
            const auto minimum = ModelParameter(record, "minimum-output");
            const auto maximum = ModelParameter(record, "maximum-output");
            const auto max_step = ModelParameter(record, "max-step");
            if (!gain.has_value() || !minimum.has_value() || !maximum.has_value() || !max_step.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            component.amplifier_model = CircuitAmplifierModel{
                .kind = component.type == CircuitComponentType::OpAmp ? "bounded-opamp" : "bounded-ota",
                .model_version = 1U,
                .gain = *gain,
                .minimum_output = *minimum,
                .maximum_output = *maximum,
                .max_step = *max_step};
            continue;
        }
        if (component.type == CircuitComponentType::Triode) {
            const auto result = model_database.Bind({*component.model, ModelKind::Triode});
            if (!result.model.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            const auto& record = *result.model;
            const auto amplification = ModelParameter(record, "amplification-factor");
            const auto perveance = ModelParameter(record, "perveance");
            const auto max_current = ModelParameter(record, "max-current");
            const auto max_step = ModelParameter(record, "max-step");
            if (!amplification.has_value() || !perveance.has_value() ||
                !max_current.has_value() || !max_step.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            component.triode_model = CircuitTriodeModel{
                .kind = "space-charge",
                .model_version = 1U,
                .amplification_factor = *amplification,
                .perveance = *perveance,
                .max_current = *max_current,
                .max_step = *max_step};
            continue;
        }
        if (component.type == CircuitComponentType::Pentode) {
            const auto result = model_database.Bind({*component.model, ModelKind::Pentode});
            if (!result.model.has_value()) return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            const auto& record = *result.model;
            const auto amplification = ModelParameter(record, "screen-amplification-factor");
            const auto perveance = ModelParameter(record, "perveance");
            const auto knee = ModelParameter(record, "knee-voltage");
            const auto fraction = ModelParameter(record, "screen-current-fraction");
            const auto max_current = ModelParameter(record, "max-current");
            const auto max_step = ModelParameter(record, "max-step");
            if (!amplification || !perveance || !knee || !fraction || !max_current || !max_step) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            component.pentode_model = CircuitPentodeModel{.kind="space-charge", .model_version=1U,
                .screen_amplification_factor=*amplification, .perveance=*perveance, .knee_voltage=*knee,
                .screen_current_fraction=*fraction, .max_current=*max_current, .max_step=*max_step};
            continue;
        }
        if (component.diode_model.has_value()) {
            continue;
        }
        const auto diode = model_database.Bind({*component.model, ModelKind::Diode});
        const auto zener = diode.model.has_value()
            ? ModelLookupResult{} : model_database.Bind({*component.model, ModelKind::Zener});
        const auto* record = diode.model.has_value() ? &*diode.model
            : (zener.model.has_value() ? &*zener.model : nullptr);
        if (record == nullptr) {
            return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
        }
        const auto saturation = ModelParameter(*record, "saturation-current");
        const auto thermal = ModelParameter(*record, "thermal-voltage");
        const auto max_step = ModelParameter(*record, "max-step");
        const auto breakdown_voltage = ModelParameter(*record, "breakdown-voltage");
        const auto breakdown_current = ModelParameter(*record, "breakdown-current");
        const auto breakdown_slope = ModelParameter(*record, "breakdown-slope");
        const bool is_zener = record->kind == ModelKind::Zener;
        if (!saturation.has_value() || !thermal.has_value() || !max_step.has_value() ||
            (is_zener && (!breakdown_voltage.has_value() || !breakdown_current.has_value() ||
                           !breakdown_slope.has_value()))) {
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
        }
        component.diode_model = CircuitDiodeModel{
            .kind = is_zener ? "zener" : "shockley",
            .model_version = 1U,
            .saturation_current = *saturation,
            .thermal_voltage = *thermal,
            .breakdown_voltage = breakdown_voltage.value_or(0.0),
            .breakdown_current = breakdown_current.value_or(0.0),
            .breakdown_slope = breakdown_slope.value_or(0.0),
            .max_step = *max_step};
    }
    return CompileMnaCircuit(bound);
}

} // namespace solo_rig
