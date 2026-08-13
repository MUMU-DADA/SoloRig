#include "content/whitebox_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>

namespace solo_rig {

namespace {

constexpr std::uint32_t kNoIndex = std::numeric_limits<std::uint32_t>::max();

std::int32_t FindNode(const std::unordered_map<std::string, std::int32_t>& nodes, std::string_view id) {
    const auto iterator = nodes.find(std::string(id));
    return iterator == nodes.end() ? -2 : iterator->second;
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

WhiteboxRuntimeError SolveMnaScattering(std::vector<double>& matrix,
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
        if (!std::isfinite(pivot_value)) {
            return WhiteboxRuntimeError::NonFinite;
        }
        if (pivot_value <= kPivotEpsilon) {
            return WhiteboxRuntimeError::SingularMatrix;
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
                return WhiteboxRuntimeError::NonFinite;
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
        if (!std::isfinite(value) || !std::isfinite(diagonal)) {
            return WhiteboxRuntimeError::NonFinite;
        }
        if (std::fabs(diagonal) <= kPivotEpsilon) {
            return WhiteboxRuntimeError::SingularMatrix;
        }
        solution[static_cast<std::size_t>(row)] = value / diagonal;
        if (!std::isfinite(solution[static_cast<std::size_t>(row)])) {
            return WhiteboxRuntimeError::NonFinite;
        }
    }
    return WhiteboxRuntimeError::None;
}

} // namespace

std::optional<WhiteboxWdfPlan> BuildWhiteboxWdfPlan(const WhiteboxCompiledGraph& graph) {
    WhiteboxWdfPlan plan{.topology = graph.Classification()};
    if (graph.Classification() == WhiteboxTopologyClass::Series) {
        plan.adaptors = {WhiteboxWdfAdaptorKind::Series, WhiteboxWdfAdaptorKind::RType};
        return plan;
    }
    if (graph.Classification() == WhiteboxTopologyClass::Parallel) {
        plan.adaptors = {WhiteboxWdfAdaptorKind::Parallel, WhiteboxWdfAdaptorKind::RType};
        return plan;
    }
    return std::nullopt;
}

void WhiteboxRuntime::Reset() noexcept {
    std::fill(capacitor_history_.begin(), capacitor_history_.end(), 0.0);
    std::fill(inductor_history_.begin(), inductor_history_.end(), 0.0);
    std::fill(solution_.begin(), solution_.end(), 0.0);
    std::fill(previous_.begin(), previous_.end(), 0.0);
    for (const auto& element : elements_) {
        if (element.kind == ElementKind::Capacitor && element.state_index != kNoIndex) {
            capacitor_history_[element.state_index] = element.initial;
        }
        if (element.kind == ElementKind::Inductor && element.state_index != kNoIndex) {
            inductor_history_[element.state_index] = element.initial;
        }
        if (element.kind == ElementKind::NonlinearAdmittance && element.positive_node >= 0) {
            solution_[static_cast<std::size_t>(element.positive_node)] = element.initial;
        }
    }
}

WhiteboxRuntimeError WhiteboxRuntime::SolveDc(double input, double& output) noexcept {
    return Process(input, output);
}

WhiteboxRuntimeError WhiteboxRuntime::Process(double input, double& output) noexcept {
    output = 0.0;
    if (!prepared_) {
        return WhiteboxRuntimeError::NotPrepared;
    }
    if (!std::isfinite(input)) {
        return WhiteboxRuntimeError::NonFinite;
    }
    const double conductance_scale = sample_rate_hz_ * static_cast<double>(oversampling_);
    for (std::uint32_t substep = 0; substep < oversampling_; ++substep) {
        const bool has_nonlinear = std::any_of(elements_.begin(), elements_.end(), [](const auto& element) {
            return element.kind == ElementKind::NonlinearAdmittance;
        });
        bool converged = !has_nonlinear;
        for (std::uint32_t iteration = 0; iteration < max_iterations_; ++iteration) {
            std::copy(solution_.begin(), solution_.end(), previous_.begin());
            std::fill(matrix_.begin(), matrix_.end(), 0.0);
            std::fill(rhs_.begin(), rhs_.end(), 0.0);
            for (const auto& element : elements_) {
                switch (element.kind) {
                case ElementKind::Resistor:
                    StampConductance(matrix_, matrix_size_, element.positive_node, element.negative_node,
                                     1.0 / element.value);
                    break;
                case ElementKind::Capacitor: {
                    const double conductance = element.value * conductance_scale;
                    StampConductance(matrix_, matrix_size_, element.positive_node, element.negative_node, conductance);
                    StampCurrent(rhs_, element.positive_node, element.negative_node,
                                 conductance * capacitor_history_[element.state_index]);
                    break;
                }
                case ElementKind::Inductor: {
                    const double inductance_over_dt = element.value * conductance_scale;
                    StampInductor(matrix_, rhs_, matrix_size_, element.positive_node, element.negative_node,
                                  element.branch_index, inductance_over_dt,
                                  inductor_history_[element.state_index]);
                    break;
                }
                case ElementKind::VoltageSource:
                    StampVoltageSource(matrix_, rhs_, matrix_size_, element.positive_node, element.negative_node,
                                       element.branch_index, element.value);
                    break;
                case ElementKind::NonlinearAdmittance: {
                    const double positive = element.positive_node >= 0
                        ? previous_[static_cast<std::size_t>(element.positive_node)] : 0.0;
                    const double negative = element.negative_node >= 0
                        ? previous_[static_cast<std::size_t>(element.negative_node)] : 0.0;
                    const double voltage = positive - negative;
                    const double current = element.value * voltage * voltage * voltage;
                    const double conductance = 3.0 * element.value * voltage * voltage;
                    if (!std::isfinite(current) || !std::isfinite(conductance)) {
                        return WhiteboxRuntimeError::NonFinite;
                    }
                    StampConductance(matrix_, matrix_size_, element.positive_node, element.negative_node, conductance);
                    StampCurrent(rhs_, element.positive_node, element.negative_node,
                                 -(current - conductance * voltage));
                    break;
                }
                }
            }
            if (input_branch_ != kNoIndex) {
                StampVoltageSource(matrix_, rhs_, matrix_size_, input_node_, -1, input_branch_, input);
            }
            const auto solve_error = SolveMnaScattering(matrix_, rhs_, solution_, matrix_size_);
            if (solve_error != WhiteboxRuntimeError::None) {
                return solve_error;
            }
            double maximum_delta = 0.0;
            double maximum_value = 1.0;
            for (std::uint32_t index = 0; index < matrix_size_; ++index) {
                maximum_delta = std::max(maximum_delta,
                                         std::fabs(solution_[index] - previous_[index]));
                maximum_value = std::max(maximum_value, std::fabs(solution_[index]));
            }
            if (!has_nonlinear || maximum_delta <= absolute_tolerance_ + relative_tolerance_ * maximum_value) {
                converged = true;
                break;
            }
        }
        if (!converged) {
            return WhiteboxRuntimeError::NonConvergent;
        }
        for (const auto& element : elements_) {
            if (element.kind == ElementKind::Capacitor && element.state_index != kNoIndex) {
                const double positive = element.positive_node >= 0
                    ? solution_[static_cast<std::size_t>(element.positive_node)] : 0.0;
                const double negative = element.negative_node >= 0
                    ? solution_[static_cast<std::size_t>(element.negative_node)] : 0.0;
                capacitor_history_[element.state_index] = positive - negative;
            }
            if (element.kind == ElementKind::Inductor && element.state_index != kNoIndex) {
                inductor_history_[element.state_index] = solution_[element.branch_index];
            }
        }
        output = output_node_ >= 0 ? solution_[static_cast<std::size_t>(output_node_)] : 0.0;
    }
    return std::isfinite(output) ? WhiteboxRuntimeError::None : WhiteboxRuntimeError::NonFinite;
}

WhiteboxRuntimePrepareResult PrepareWhiteboxRuntime(const WhiteboxCompiledGraph& graph,
                                                    const WhiteboxRuntimeOptions& options) {
    if (options.backend != WhiteboxRuntimeBackend::Mna && options.backend != WhiteboxRuntimeBackend::Wdf) {
        return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidBackend};
    }
    if (options.backend == WhiteboxRuntimeBackend::Wdf || graph.Backend() == WhiteboxBackend::Wdf) {
        return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::UnsupportedBackend};
    }
    if (!std::isfinite(options.sample_rate_hz) || options.sample_rate_hz <= 0.0 ||
        options.oversampling == 0U || options.oversampling > 16U || options.max_iterations == 0U ||
        options.max_iterations > 1024U || !std::isfinite(options.absolute_tolerance) ||
        !std::isfinite(options.relative_tolerance) || options.absolute_tolerance <= 0.0 ||
        options.relative_tolerance <= 0.0 || !std::isfinite(options.maximum_initial_state) ||
        options.maximum_initial_state <= 0.0) {
        return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidOptions};
    }

    std::unordered_map<std::string, std::int32_t> node_map;
    std::int32_t non_ground_nodes{};
    for (const auto& node : graph.Nodes()) {
        if (node.id == graph.ReferenceGround()) {
            continue;
        }
        node_map.emplace(node.id, non_ground_nodes++);
    }
    // The reference node is represented by the conventional -1 stamp index;
    // keeping it in the lookup makes every pin use the same validated path.
    node_map.emplace(std::string(graph.ReferenceGround()), -1);
    auto find_port_node = [&graph](WhiteboxPortDirection direction) -> std::optional<std::string> {
        const auto iterator = std::find_if(graph.Ports().begin(), graph.Ports().end(),
                                           [direction](const auto& port) { return port.direction == direction; });
        return iterator == graph.Ports().end() ? std::nullopt : std::optional<std::string>(iterator->node);
    };
    const auto input_port_node = find_port_node(WhiteboxPortDirection::Input);
    const auto output_port_node = find_port_node(WhiteboxPortDirection::Output);
    if (!input_port_node.has_value() || !output_port_node.has_value()) {
        return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidTopology};
    }
    const auto input_node = FindNode(node_map, *input_port_node);
    const auto output_node = FindNode(node_map, *output_port_node);
    if (input_node < 0 || output_node < -1) {
        return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidTopology};
    }

    WhiteboxRuntime runtime;
    runtime.backend_ = WhiteboxRuntimeBackend::Mna;
    runtime.input_node_ = input_node;
    runtime.output_node_ = output_node;
    runtime.sample_rate_hz_ = options.sample_rate_hz;
    runtime.oversampling_ = options.oversampling;
    runtime.max_iterations_ = options.max_iterations;
    runtime.absolute_tolerance_ = options.absolute_tolerance;
    runtime.relative_tolerance_ = options.relative_tolerance;

    std::uint32_t capacitor_state{};
    std::uint32_t inductor_state{};
    for (const auto& component : graph.Components()) {
        if (component.pins.size() != 2U) {
            return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidTopology};
        }
        const auto positive = FindNode(node_map, component.pins[0]);
        const auto negative = FindNode(node_map, component.pins[1]);
        if (positive == -2 || negative == -2 || positive == negative) {
            return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidTopology};
        }
        if (!component.value.has_value() || !std::isfinite(*component.value)) {
            return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidValue};
        }
        const double initial = component.initial.value_or(0.0);
        if (!std::isfinite(initial) || std::fabs(initial) > options.maximum_initial_state) {
            return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidInitialState};
        }
        WhiteboxRuntime::Element element{.positive_node = positive,
                                         .negative_node = negative,
                                         .branch_index = kNoIndex,
                                         .state_index = kNoIndex,
                                         .value = *component.value,
                                         .initial = initial};
        if (component.kind == "resistor") {
            if (*component.value <= 0.0) {
                return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidValue};
            }
            element.kind = WhiteboxRuntime::ElementKind::Resistor;
        } else if (component.kind == "capacitor") {
            if (*component.value <= 0.0) {
                return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidValue};
            }
            element.kind = WhiteboxRuntime::ElementKind::Capacitor;
            element.state_index = capacitor_state++;
        } else if (component.kind == "inductor") {
            if (*component.value <= 0.0) {
                return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidValue};
            }
            element.kind = WhiteboxRuntime::ElementKind::Inductor;
            element.state_index = inductor_state++;
        } else if (component.kind == "voltage-source") {
            element.kind = WhiteboxRuntime::ElementKind::VoltageSource;
        } else if (component.kind == "nonlinear-admittance") {
            if (*component.value <= 0.0 || !component.nonlinear_group.has_value()) {
                return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidValue};
            }
            element.kind = WhiteboxRuntime::ElementKind::NonlinearAdmittance;
        } else {
            return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::UnsupportedComponent};
        }
        runtime.elements_.push_back(element);
    }
    for (const auto& supply : graph.Supplies()) {
        const auto positive = FindNode(node_map, supply.positive);
        const auto negative = FindNode(node_map, supply.negative);
        if (positive == -2 || negative == -2 || positive == negative || !std::isfinite(supply.voltage)) {
            return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidValue};
        }
        runtime.elements_.push_back({.kind = WhiteboxRuntime::ElementKind::VoltageSource,
                                     .positive_node = positive,
                                     .negative_node = negative,
                                     .branch_index = kNoIndex,
                                     .state_index = kNoIndex,
                                     .value = supply.voltage,
                                     .initial = 0.0});
    }
    std::uint32_t branch = static_cast<std::uint32_t>(non_ground_nodes);
    for (auto& element : runtime.elements_) {
        if (element.kind == WhiteboxRuntime::ElementKind::VoltageSource ||
            element.kind == WhiteboxRuntime::ElementKind::Inductor) {
            element.branch_index = branch++;
        }
    }
    runtime.input_branch_ = branch++;
    runtime.matrix_size_ = branch;
    if (runtime.matrix_size_ == 0U) {
        return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidTopology};
    }
    runtime.capacitor_history_.assign(capacitor_state, 0.0);
    runtime.inductor_history_.assign(inductor_state, 0.0);
    runtime.matrix_.assign(static_cast<std::size_t>(runtime.matrix_size_) * runtime.matrix_size_, 0.0);
    runtime.rhs_.assign(runtime.matrix_size_, 0.0);
    runtime.solution_.assign(runtime.matrix_size_, 0.0);
    runtime.previous_.assign(runtime.matrix_size_, 0.0);
    runtime.Reset();
    runtime.prepared_ = true;
    return {.runtime = std::move(runtime), .error = std::nullopt};
}

} // namespace solo_rig
