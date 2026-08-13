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

void StampDiode(std::vector<double>& matrix,
                std::vector<double>& rhs,
                std::uint32_t size,
                const MnaPreparedCircuit::Element& element,
                const std::vector<double>& guess) noexcept {
    const auto positive = element.positive_node >= 0 ? guess[static_cast<std::size_t>(element.positive_node)] : 0.0;
    const auto negative = element.negative_node >= 0 ? guess[static_cast<std::size_t>(element.negative_node)] : 0.0;
    const auto voltage = positive - negative;
    const auto forward = std::exp(std::clamp(voltage / element.diode_thermal_voltage, -60.0, 60.0));
    const auto current = element.diode_saturation_current * (forward - 1.0);
    const auto conductance = element.diode_saturation_current / element.diode_thermal_voltage * forward;
    const auto equivalent_current = current - conductance * voltage;
    StampConductance(matrix, size, element.positive_node, element.negative_node, conductance);
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
            return element.type == CircuitComponentType::Diode || element.controlled_has_limits;
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
                case CircuitComponentType::Vccs:
                    StampVccs(matrix_, rhs_, matrix_size_, element, newton_previous_);
                    break;
                case CircuitComponentType::IdealOpAmp:
                    StampIdealOpAmp(matrix_, rhs_, matrix_size_, element, newton_previous_);
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
            bool step_exceeded = false;
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
                    if (std::fabs((new_positive - new_negative) - (old_positive - old_negative)) > element.diode_max_step) {
                        step_exceeded = true;
                    }
                }
            }
            if (step_exceeded) {
                return {.error = MnaSolveError::NonConvergent};
            }
            if (!has_nonlinearity || maximum_delta <= absolute_tolerance_ + relative_tolerance_ * maximum_value) {
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
        if (component.type != CircuitComponentType::Resistor && component.type != CircuitComponentType::Capacitor &&
            component.type != CircuitComponentType::Inductor &&
            component.type != CircuitComponentType::VoltageSource && component.type != CircuitComponentType::Diode &&
            component.type != CircuitComponentType::Potentiometer && component.type != CircuitComponentType::Vccs &&
            component.type != CircuitComponentType::IdealOpAmp) {
            return {.circuit = std::nullopt, .error = MnaCompileError::UnsupportedComponent};
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
            return component.type == CircuitComponentType::IdealOpAmp;
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
        const auto negative = FindNode(circuit, component.pins[1]);
        if (positive == -2 || negative == -2 || positive == negative) {
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
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
                                                .diode_saturation_current = 0.0,
                                                .diode_thermal_voltage = 0.0,
                                                .diode_max_step = 0.0,
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
                                            .diode_saturation_current = 0.0,
                                            .diode_thermal_voltage = 0.0,
                                            .diode_max_step = 0.0};
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
            if (!component.diode_model.has_value() || component.diode_model->kind != "shockley" ||
                !std::isfinite(component.diode_model->saturation_current) ||
                !std::isfinite(component.diode_model->thermal_voltage) ||
                !std::isfinite(component.diode_model->max_step) || component.diode_model->saturation_current <= 0.0 ||
                component.diode_model->thermal_voltage <= 0.0 || component.diode_model->max_step <= 0.0) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            element.diode_saturation_current = component.diode_model->saturation_current;
            element.diode_thermal_voltage = component.diode_model->thermal_voltage;
            element.diode_max_step = component.diode_model->max_step;
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

} // namespace solo_rig
