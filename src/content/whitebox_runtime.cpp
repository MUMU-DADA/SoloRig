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

WhiteboxRuntimeError MapWdfError(WdfError error) noexcept {
    switch (error) {
    case WdfError::None:
        return WhiteboxRuntimeError::None;
    case WdfError::NonFinite:
        return WhiteboxRuntimeError::NonFinite;
    case WdfError::NotPrepared:
        return WhiteboxRuntimeError::NotPrepared;
    case WdfError::InvalidValue:
        return WhiteboxRuntimeError::InvalidValue;
    case WdfError::InvalidTopology:
        return WhiteboxRuntimeError::InvalidTopology;
    }
    return WhiteboxRuntimeError::InvalidValue;
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

WhiteboxRuntime::WhiteboxRuntime() noexcept
    : elements_{},
      capacitor_history_{},
      inductor_history_{},
      matrix_{},
      rhs_{},
      solution_{},
      previous_{},
      wdf_ports_{},
      wdf_adaptor_{},
      wdf_incident_{},
      wdf_reflected_{},
      wdf_resistances_{},
      wdf_output_position_{},
      wdf_network_kind_{WdfNetworkKind::SeriesOpen},
      input_node_{-1},
      output_node_{-1},
      input_branch_{kNoIndex},
      matrix_size_{},
      oversampling_{1},
      max_iterations_{1},
      sample_rate_hz_{48000.0},
      absolute_tolerance_{1.0e-9},
      relative_tolerance_{1.0e-6},
      backend_{WhiteboxRuntimeBackend::Mna},
      prepared_{false} {}

void WhiteboxRuntime::Reset() noexcept {
    if (backend_ == WhiteboxRuntimeBackend::Wdf) {
        for (auto& port : wdf_ports_) {
            port.Reset();
        }
        if (wdf_adaptor_.has_value()) {
            wdf_adaptor_->Reset();
        }
        return;
    }
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

bool WhiteboxRuntime::SetSampleRate(double sample_rate_hz) noexcept {
    if (!prepared_ || !std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return false;
    }
    if (backend_ == WhiteboxRuntimeBackend::Wdf) {
        for (const auto& port : wdf_ports_) {
            if (!port.CanSetSampleRate(sample_rate_hz)) {
                return false;
            }
        }
        for (auto& port : wdf_ports_) {
            if (!port.SetSampleRate(sample_rate_hz)) {
                return false;
            }
        }
        if (wdf_resistances_.size() != wdf_ports_.size() || !wdf_adaptor_.has_value()) {
            return false;
        }
        for (std::size_t index = 0; index < wdf_ports_.size(); ++index) {
            wdf_resistances_[index] = wdf_ports_[index].PortResistance();
        }
        if (!wdf_adaptor_->SetResistances(wdf_resistances_)) {
            return false;
        }
    }
    sample_rate_hz_ = sample_rate_hz;
    return true;
}

WhiteboxRuntimeError WhiteboxRuntime::Process(double input, double& output) noexcept {
    output = 0.0;
    if (!prepared_) {
        return WhiteboxRuntimeError::NotPrepared;
    }
    if (!std::isfinite(input)) {
        return WhiteboxRuntimeError::NonFinite;
    }
    if (backend_ == WhiteboxRuntimeBackend::Wdf) {
        if (!wdf_adaptor_.has_value() || wdf_ports_.empty() ||
            wdf_incident_.size() != wdf_ports_.size() ||
            wdf_reflected_.size() != wdf_ports_.size()) {
            return WhiteboxRuntimeError::NotPrepared;
        }
        for (std::size_t index = 0; index < wdf_ports_.size(); ++index) {
            wdf_reflected_[index] = wdf_ports_[index].ReflectedWave();
            if (!std::isfinite(wdf_reflected_[index]) ||
                !std::isfinite(wdf_ports_[index].PortResistance()) ||
                wdf_ports_[index].PortResistance() <= 0.0) {
                return WhiteboxRuntimeError::NonFinite;
            }
        }
        if (wdf_network_kind_ == WdfNetworkKind::SeriesGrounded ||
            wdf_network_kind_ == WdfNetworkKind::SeriesOpen) {
            double total_resistance = 0.0;
            double reflected_voltage = 0.0;
            for (std::size_t index = 0; index < wdf_ports_.size(); ++index) {
                total_resistance += wdf_ports_[index].PortResistance();
                reflected_voltage += wdf_reflected_[index];
            }
            if (!std::isfinite(total_resistance) || total_resistance <= 0.0 ||
                !std::isfinite(reflected_voltage)) {
                return WhiteboxRuntimeError::NonFinite;
            }
            const double current = wdf_network_kind_ == WdfNetworkKind::SeriesGrounded
                ? (input - reflected_voltage) / total_resistance
                : 0.0;
            if (wdf_network_kind_ == WdfNetworkKind::SeriesGrounded) {
                const auto scatter_error = wdf_adaptor_->Scatter(wdf_reflected_, wdf_incident_);
                if (scatter_error != WdfError::None) {
                    return MapWdfError(scatter_error);
                }
                for (std::size_t index = 0; index < wdf_ports_.size(); ++index) {
                    wdf_incident_[index] +=
                        2.0 * wdf_ports_[index].PortResistance() * input / total_resistance;
                }
            } else {
                std::copy(wdf_reflected_.begin(), wdf_reflected_.end(), wdf_incident_.begin());
            }
            output = input;
            for (std::size_t index = 0; index < wdf_ports_.size(); ++index) {
                const double resistance = wdf_ports_[index].PortResistance();
                double reflected{};
                const auto error = wdf_ports_[index].Process(wdf_incident_[index], reflected);
                if (error != WdfError::None) {
                    return MapWdfError(error);
                }
                if (index < wdf_output_position_) {
                    output -= wdf_reflected_[index] + resistance * current;
                }
            }
        } else {
            double branch_voltage = input;
            if (wdf_network_kind_ == WdfNetworkKind::ParallelFloating) {
                const auto scatter_error = wdf_adaptor_->Scatter(wdf_reflected_, wdf_incident_);
                if (scatter_error != WdfError::None) {
                    return MapWdfError(scatter_error);
                }
                branch_voltage = (wdf_incident_.front() + wdf_reflected_.front()) * 0.5;
            } else {
                for (std::size_t index = 0; index < wdf_ports_.size(); ++index) {
                    wdf_incident_[index] = 2.0 * branch_voltage - wdf_reflected_[index];
                }
            }
            for (std::size_t index = 0; index < wdf_ports_.size(); ++index) {
                double reflected{};
                const auto error = wdf_ports_[index].Process(wdf_incident_[index], reflected);
                if (error != WdfError::None) {
                    return MapWdfError(error);
                }
            }
            output = wdf_network_kind_ == WdfNetworkKind::ParallelFloating
                ? input - branch_voltage
                : (output_node_ >= 0 ? input : 0.0);
        }
        return std::isfinite(output) ? WhiteboxRuntimeError::None : WhiteboxRuntimeError::NonFinite;
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
    if (!std::isfinite(options.sample_rate_hz) || options.sample_rate_hz <= 0.0 ||
        options.oversampling == 0U || options.oversampling > 16U || options.max_iterations == 0U ||
        options.max_iterations > 1024U || !std::isfinite(options.absolute_tolerance) ||
        !std::isfinite(options.relative_tolerance) || options.absolute_tolerance <= 0.0 ||
        options.relative_tolerance <= 0.0 || !std::isfinite(options.maximum_initial_state) ||
        options.maximum_initial_state <= 0.0) {
        return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidOptions};
    }
    if (options.backend == WhiteboxRuntimeBackend::Wdf && options.oversampling != 1U) {
        // M18-A owns the base-rate scattering runtime. Oversampled WDF stage
        // assembly remains a later milestone and must not be silently ignored.
        return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidOptions};
    }
    // A graph explicitly requesting WDF cannot silently fall back to MNA.
    // Conversely, an explicit WDF option is allowed to select the WDF
    // lowering for a graph whose preferred backend is MNA.
    if (options.backend == WhiteboxRuntimeBackend::Mna && graph.Backend() == WhiteboxBackend::Wdf) {
        return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::UnsupportedBackend};
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

    if (options.backend == WhiteboxRuntimeBackend::Wdf) {
        if (!graph.Supplies().empty() || graph.Components().empty()) {
            return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::UnsupportedComponent};
        }
        WdfAdaptorKind adaptor_kind{};
        if (graph.Classification() == WhiteboxTopologyClass::Series) {
            adaptor_kind = WdfAdaptorKind::Series;
        } else if (graph.Classification() == WhiteboxTopologyClass::Parallel) {
            adaptor_kind = WdfAdaptorKind::Parallel;
        } else {
            return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidTopology};
        }

        WhiteboxRuntime runtime{};
        runtime.backend_ = WhiteboxRuntimeBackend::Wdf;
        runtime.input_node_ = input_node;
        runtime.output_node_ = output_node;
        runtime.sample_rate_hz_ = options.sample_rate_hz;
        runtime.oversampling_ = options.oversampling;
        runtime.max_iterations_ = options.max_iterations;
        runtime.absolute_tolerance_ = options.absolute_tolerance;
        runtime.relative_tolerance_ = options.relative_tolerance;

        std::vector<std::size_t> component_order;
        component_order.reserve(graph.Components().size());
        if (graph.Classification() == WhiteboxTopologyClass::Series) {
            std::vector<bool> used(graph.Components().size(), false);
            std::string current = *input_port_node;
            std::optional<std::size_t> output_position;
            if (current == *output_port_node) {
                output_position = 0U;
            }
            while (component_order.size() < graph.Components().size() && current != graph.ReferenceGround()) {
                std::size_t selected = graph.Components().size();
                std::string next;
                for (std::size_t index = 0; index < graph.Components().size(); ++index) {
                    if (used[index] || graph.Components()[index].pins.size() != 2U) {
                        continue;
                    }
                    const auto& pins = graph.Components()[index].pins;
                    if (pins[0] == current || pins[1] == current) {
                        if (selected != graph.Components().size()) {
                            return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidTopology};
                        }
                        selected = index;
                        next = pins[0] == current ? pins[1] : pins[0];
                    }
                }
                if (selected == graph.Components().size()) {
                    break;
                }
                used[selected] = true;
                component_order.push_back(selected);
                current = std::move(next);
                if (current == *output_port_node) {
                    output_position = component_order.size();
                }
            }
            if (component_order.size() != graph.Components().size() || !output_position.has_value()) {
                return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidTopology};
            }
            runtime.wdf_output_position_ = *output_position;
            runtime.wdf_network_kind_ = current == graph.ReferenceGround()
                ? WhiteboxRuntime::WdfNetworkKind::SeriesGrounded
                : WhiteboxRuntime::WdfNetworkKind::SeriesOpen;
        } else {
            const auto& first_pins = graph.Components().front().pins;
            if (first_pins.size() != 2U) {
                return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidTopology};
            }
            const bool input_is_first = first_pins[0] == *input_port_node;
            const bool input_is_second = first_pins[1] == *input_port_node;
            if (!input_is_first && !input_is_second) {
                return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidTopology};
            }
            const std::string& other = input_is_first ? first_pins[1] : first_pins[0];
            for (std::size_t index = 0; index < graph.Components().size(); ++index) {
                const auto& pins = graph.Components()[index].pins;
                if (pins.size() != 2U ||
                    !((pins[0] == *input_port_node && pins[1] == other) ||
                      (pins[1] == *input_port_node && pins[0] == other))) {
                    return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidTopology};
                }
                component_order.push_back(index);
            }
            if (other == graph.ReferenceGround()) {
                if (*output_port_node != *input_port_node && *output_port_node != graph.ReferenceGround()) {
                    return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidTopology};
                }
                runtime.wdf_network_kind_ = WhiteboxRuntime::WdfNetworkKind::ParallelDriven;
            } else if (other == *output_port_node) {
                runtime.wdf_network_kind_ = WhiteboxRuntime::WdfNetworkKind::ParallelFloating;
            } else {
                return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidTopology};
            }
        }

        std::vector<double> resistances;
        resistances.reserve(graph.Components().size());
        for (const auto component_index : component_order) {
            const auto& component = graph.Components()[component_index];
            if (component.pins.size() != 2U || component.model_reference.has_value() ||
                !component.value.has_value() || !std::isfinite(*component.value) || *component.value <= 0.0) {
                return {.runtime = std::nullopt,
                        .error = component.pins.size() != 2U
                            ? WhiteboxRuntimeError::InvalidTopology
                            : WhiteboxRuntimeError::InvalidValue};
            }
            const double initial = component.initial.value_or(0.0);
            if (!std::isfinite(initial) || std::fabs(initial) > options.maximum_initial_state) {
                return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::InvalidInitialState};
            }
            WdfElementKind kind{};
            if (component.kind == "resistor") {
                kind = WdfElementKind::Resistor;
            } else if (component.kind == "capacitor") {
                kind = WdfElementKind::Capacitor;
            } else if (component.kind == "inductor") {
                kind = WdfElementKind::Inductor;
            } else if (component.kind == "transformer") {
                // A transformer component without a model field is the
                // clean-room unit-ratio mathematical fixture. Specific
                // device models remain outside this milestone.
                kind = WdfElementKind::Transformer;
            } else {
                return {.runtime = std::nullopt, .error = WhiteboxRuntimeError::UnsupportedComponent};
            }
            const auto prepared = PrepareWdfOnePort(kind, *component.value, options.sample_rate_hz, initial);
            if (!prepared.port.has_value() || prepared.error.has_value()) {
                return {.runtime = std::nullopt,
                        .error = prepared.error.has_value() ? MapWdfError(*prepared.error)
                                                            : WhiteboxRuntimeError::InvalidValue};
            }
            resistances.push_back(prepared.port->PortResistance());
            runtime.wdf_ports_.emplace_back();
            runtime.wdf_ports_.back().CopyPreparedState(*prepared.port);
        }
        const auto adaptor = PrepareWdfAdaptor(adaptor_kind, resistances);
        if (!adaptor.adaptor.has_value() || adaptor.error.has_value()) {
            return {.runtime = std::nullopt,
                    .error = adaptor.error.has_value() ? MapWdfError(*adaptor.error)
                                                       : WhiteboxRuntimeError::InvalidTopology};
        }
        runtime.wdf_adaptor_ = std::move(*adaptor.adaptor);
        runtime.wdf_incident_.assign(runtime.wdf_ports_.size(), 0.0);
        runtime.wdf_reflected_.assign(runtime.wdf_ports_.size(), 0.0);
        runtime.wdf_resistances_ = std::move(resistances);
        runtime.Reset();
        runtime.prepared_ = true;
        return {.runtime = std::move(runtime), .error = std::nullopt};
    }

    WhiteboxRuntime runtime{};
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
