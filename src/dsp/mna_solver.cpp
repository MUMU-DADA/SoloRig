#include "dsp/mna_solver.hpp"

#include "content/nonlinear_bjt_binding.hpp"
#include "content/nonlinear_jfet_binding.hpp"
#include "content/nonlinear_mosfet_binding.hpp"
#include "content/nonlinear_opamp_binding.hpp"
#include "content/dynamic_speaker_load_binding.hpp"

#include "content/nonlinear_diode_binding.hpp"

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

void StampDynamicPowerSupply(std::vector<double>& matrix,
                             std::vector<double>& rhs,
                             std::uint32_t size,
                             const MnaPreparedCircuit::Element& element,
                             const std::vector<double>& guess,
                             const DynamicPowerSupplyState& previous_state,
                             double timestep_seconds) noexcept {
    const double branch_current = guess[element.branch_index];
    const auto evaluation = EvaluateDynamicPowerSupply(
        element.dynamic_power_supply_model, branch_current, previous_state, timestep_seconds);
    if (!evaluation.Succeeded()) {
        rhs[element.branch_index] = std::numeric_limits<double>::quiet_NaN();
        return;
    }
    StampVoltageSource(matrix, rhs, size, element.positive_node, element.negative_node,
                       element.branch_index, evaluation.target_voltage -
                           evaluation.target_voltage_derivative_current * branch_current);
    matrix[static_cast<std::size_t>(element.branch_index) * size + element.branch_index] -=
        element.dynamic_power_supply_model.source_resistance +
        evaluation.target_voltage_derivative_current;
}

void StampDynamicRelay(std::vector<double>& matrix,
                       std::vector<double>& rhs,
                       std::uint32_t size,
                       const MnaPreparedCircuit::Element& element,
                       const std::vector<double>& guess,
                       const DynamicRelayState& previous_state,
                       double timestep_seconds) noexcept {
    const double current = guess[element.branch_index];
    const auto evaluation = EvaluateDynamicRelay(
        element.dynamic_relay_model, current, previous_state, timestep_seconds);
    if (!evaluation.Succeeded()) {
        rhs[element.branch_index] = std::numeric_limits<double>::quiet_NaN();
        return;
    }
    StampVoltageSource(matrix, rhs, size, element.positive_node, element.negative_node,
        element.branch_index, evaluation.coil_voltage -
            evaluation.coil_voltage_derivative_current * current);
    matrix[static_cast<std::size_t>(element.branch_index) * size + element.branch_index] -=
        evaluation.coil_voltage_derivative_current;
    if (element.dynamic_relay_model_version == 1U) {
        StampConductance(matrix, size, element.secondary_positive_node,
            element.secondary_negative_node, 1.0 / evaluation.contact_resistance);
        return;
    }
    const auto routing = EvaluateDynamicRelayRouting(
        element.dynamic_relay_model, element.dynamic_relay_routing_model, previous_state);
    if (!routing.Succeeded()) {
        rhs[element.branch_index] = std::numeric_limits<double>::quiet_NaN();
        return;
    }
    for (std::uint32_t pole = 0; pole < element.dynamic_relay_routing_model.pole_count; ++pole) {
        StampConductance(matrix, size, element.relay_common_nodes[pole],
            element.relay_normally_closed_nodes[pole], 1.0 / routing.normally_closed_resistance);
        StampConductance(matrix, size, element.relay_common_nodes[pole],
            element.relay_normally_open_nodes[pole], 1.0 / routing.normally_open_resistance);
    }
}

void StampDynamicSpeakerLoad(std::vector<double>& matrix,
                             std::vector<double>& rhs,
                             std::uint32_t size,
                             const MnaPreparedCircuit::Element& element,
                             const std::vector<double>& guess,
                             const DynamicSpeakerLoadState& previous_state,
                             double timestep_seconds) noexcept {
    const double branch_current = guess[element.branch_index];
    const auto evaluation = EvaluateDynamicSpeakerLoad(
        element.dynamic_speaker_load_model, branch_current, previous_state, timestep_seconds);
    if (!evaluation.Succeeded()) {
        rhs[element.branch_index] = std::numeric_limits<double>::quiet_NaN();
        return;
    }
    StampVoltageSource(matrix, rhs, size, element.positive_node, element.negative_node,
                       element.branch_index, evaluation.voltage -
                           evaluation.voltage_derivative_current * branch_current);
    matrix[static_cast<std::size_t>(element.branch_index) * size + element.branch_index] -=
        evaluation.voltage_derivative_current;
}

void StampOptocoupler(std::vector<double>& matrix,
                      std::vector<double>& rhs,
                      std::uint32_t size,
                      const MnaPreparedCircuit::Element& element,
                      const std::vector<double>& guess,
                      double previous_illumination,
                      double timestep_seconds) noexcept {
    const double emitter_positive = element.positive_node >= 0
        ? guess[static_cast<std::size_t>(element.positive_node)] : 0.0;
    const double emitter_negative = element.negative_node >= 0
        ? guess[static_cast<std::size_t>(element.negative_node)] : 0.0;
    GenericOptocouplerMnaEvaluation evaluation{};
    const auto error = EvaluateGenericOptocouplerMna(
        element.optocoupler_mna_model, emitter_positive - emitter_negative,
        previous_illumination, timestep_seconds, evaluation);
    if (error != GenericOptocouplerError::None) {
        rhs.front() = std::numeric_limits<double>::quiet_NaN();
        return;
    }
    StampConductance(matrix, size, element.positive_node, element.negative_node,
                     1.0 / element.optocoupler_mna_model.emitter_resistance_ohms);
    StampConductance(matrix, size, element.secondary_positive_node,
                     element.secondary_negative_node, 1.0 / evaluation.ldr_resistance_ohms);
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

void StampCoupledInductor(std::vector<double>& matrix,
                          std::vector<double>& rhs,
                          std::uint32_t size,
                          const MnaPreparedCircuit::Element& element,
                          double inverse_dt,
                          double primary_previous,
                          double secondary_previous) noexcept {
    const double primary_dynamic = element.coupled_inductor_coefficients.primary_inductance * inverse_dt;
    const double secondary_dynamic = element.coupled_inductor_coefficients.secondary_inductance * inverse_dt;
    const double mutual_dynamic = element.coupled_inductor_coefficients.mutual_inductance * inverse_dt;
    StampInductor(matrix, rhs, size, element.positive_node, element.negative_node,
                  element.branch_index, element.coupled_inductor_model.primary_resistance + primary_dynamic,
                  primary_previous);
    StampInductor(matrix, rhs, size, element.secondary_positive_node, element.secondary_negative_node,
                  element.secondary_branch_index,
                  element.coupled_inductor_model.secondary_resistance + secondary_dynamic,
                  secondary_previous);
    rhs[element.branch_index] += element.coupled_inductor_model.primary_resistance * primary_previous;
    rhs[element.secondary_branch_index] +=
        element.coupled_inductor_model.secondary_resistance * secondary_previous;
    matrix[static_cast<std::size_t>(element.branch_index) * size + element.secondary_branch_index] -=
        mutual_dynamic;
    matrix[static_cast<std::size_t>(element.secondary_branch_index) * size + element.branch_index] -=
        mutual_dynamic;
    rhs[element.branch_index] -= mutual_dynamic * secondary_previous;
    rhs[element.secondary_branch_index] -= mutual_dynamic * primary_previous;
}

void StampNonlinearCoupledInductor(std::vector<double>& matrix,
                                   std::vector<double>& rhs,
                                   std::uint32_t size,
                                   const MnaPreparedCircuit::Element& element,
                                   double inverse_dt,
                                   double primary_previous,
                                   double secondary_previous,
                                   double primary_guess,
                                   double secondary_guess,
                                   double previous_flux,
                                   double hysteresis) noexcept {
    const double n = element.coupled_inductor_model.turns_ratio;
    const double magnetizing_previous = primary_guess + secondary_guess / n;
    const auto& model = element.nonlinear_transformer_core_model;
    const auto evaluation = EvaluateNonlinearTransformerCore(
        model, magnetizing_previous, hysteresis);
    const double derivative = evaluation.Succeeded() ? evaluation.differential_flux : std::numeric_limits<double>::quiet_NaN();
    const double primary_dynamic = element.coupled_inductor_model.primary_leakage_inductance * inverse_dt;
    const double secondary_dynamic = element.coupled_inductor_model.secondary_leakage_inductance * inverse_dt;
    const double core_dynamic = derivative * inverse_dt;
    const double correction = evaluation.Succeeded()
        ? (evaluation.flux - derivative * magnetizing_previous - previous_flux) * inverse_dt
        : std::numeric_limits<double>::quiet_NaN();
    auto stamp_branch_voltage = [&matrix, size](std::int32_t positive, std::int32_t negative,
                                                  std::uint32_t branch) {
        if (positive >= 0) {
            matrix[static_cast<std::size_t>(positive) * size + branch] += 1.0;
            matrix[static_cast<std::size_t>(branch) * size + static_cast<std::size_t>(positive)] += 1.0;
        }
        if (negative >= 0) {
            matrix[static_cast<std::size_t>(negative) * size + branch] -= 1.0;
            matrix[static_cast<std::size_t>(branch) * size + static_cast<std::size_t>(negative)] -= 1.0;
        }
    };
    stamp_branch_voltage(element.positive_node, element.negative_node, element.branch_index);
    stamp_branch_voltage(element.secondary_positive_node, element.secondary_negative_node,
                         element.secondary_branch_index);
    matrix[static_cast<std::size_t>(element.branch_index) * size + element.branch_index] -=
        element.coupled_inductor_model.primary_resistance + primary_dynamic + core_dynamic;
    matrix[static_cast<std::size_t>(element.branch_index) * size + element.secondary_branch_index] -=
        core_dynamic / n;
    matrix[static_cast<std::size_t>(element.secondary_branch_index) * size + element.branch_index] -=
        core_dynamic / n;
    matrix[static_cast<std::size_t>(element.secondary_branch_index) * size + element.secondary_branch_index] -=
        element.coupled_inductor_model.secondary_resistance + secondary_dynamic + core_dynamic / (n * n);
    rhs[element.branch_index] -= primary_dynamic * primary_previous;
    rhs[element.secondary_branch_index] -= secondary_dynamic * secondary_previous;
    rhs[element.branch_index] += correction;
    rhs[element.secondary_branch_index] += correction / n;
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

void StampDynamicOpAmp(std::vector<double>& matrix,
                       std::vector<double>& rhs,
                       std::uint32_t size,
                       const MnaPreparedCircuit::Element& element,
                       const std::vector<double>& guess,
                       double previous_state,
                       double timestep) noexcept {
    const double voltages[4] = {
        element.positive_node >= 0 ? guess[static_cast<std::size_t>(element.positive_node)] : 0.0,
        element.negative_node >= 0 ? guess[static_cast<std::size_t>(element.negative_node)] : 0.0,
        element.control_positive_node >= 0 ? guess[static_cast<std::size_t>(element.control_positive_node)] : 0.0,
        element.control_negative_node >= 0 ? guess[static_cast<std::size_t>(element.control_negative_node)] : 0.0};
    const auto evaluation = EvaluateNonlinearOpAmpDynamic(
        element.nonlinear_opamp_dynamic_model, voltages[0], voltages[1], voltages[2], voltages[3],
        previous_state, timestep);
    const std::int32_t nodes[4] = {element.positive_node, element.negative_node,
                                   element.control_positive_node, element.control_negative_node};
    if (!evaluation.Succeeded()) {
        if (element.positive_node >= 0) {
            rhs[static_cast<std::size_t>(element.positive_node)] = std::numeric_limits<double>::quiet_NaN();
        }
        return;
    }
    for (std::size_t row = 0; row < 4U; ++row) {
        if (nodes[row] < 0) continue;
        double equivalent = evaluation.currents[row];
        for (std::size_t column = 0; column < 4U; ++column) {
            equivalent -= evaluation.jacobian[row][column] * voltages[column];
            if (nodes[column] >= 0) {
                matrix[static_cast<std::size_t>(nodes[row]) * size +
                       static_cast<std::size_t>(nodes[column])] += evaluation.jacobian[row][column];
            }
        }
        rhs[static_cast<std::size_t>(nodes[row])] -= equivalent;
    }
}

void StampBjt(std::vector<double>& matrix,
              std::vector<double>& rhs,
              std::uint32_t size,
              const MnaPreparedCircuit::Element& element,
              const std::vector<double>& guess,
              double conductance_scale,
              double previous_be_voltage,
              double previous_bc_voltage) noexcept {
    const auto collector = element.positive_node >= 0 ? guess[static_cast<std::size_t>(element.positive_node)] : 0.0;
    const auto base = element.bjt_base_node >= 0 ? guess[static_cast<std::size_t>(element.bjt_base_node)] : 0.0;
    const auto emitter = element.negative_node >= 0 ? guess[static_cast<std::size_t>(element.negative_node)] : 0.0;
    const auto evaluation = EvaluateNonlinearBjtDynamic(
        element.nonlinear_bjt_dynamic_model, collector, base, emitter,
        previous_be_voltage, previous_bc_voltage, conductance_scale);
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
               const std::vector<double>& guess,
               double conductance_scale,
               double previous_gs_voltage,
               double previous_gd_voltage) noexcept {
    const auto drain = element.positive_node >= 0 ? guess[static_cast<std::size_t>(element.positive_node)] : 0.0;
    const auto gate = element.jfet_gate_node >= 0 ? guess[static_cast<std::size_t>(element.jfet_gate_node)] : 0.0;
    const auto source = element.negative_node >= 0 ? guess[static_cast<std::size_t>(element.negative_node)] : 0.0;
    const auto evaluation = EvaluateNonlinearJfetDynamic(
        element.nonlinear_jfet_dynamic_model, drain, gate, source,
        previous_gs_voltage, previous_gd_voltage, conductance_scale);
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

void StampMosfetDynamic(std::vector<double>& matrix,
                        std::vector<double>& rhs,
                        std::uint32_t size,
                        const MnaPreparedCircuit::Element& element,
                        const std::vector<double>& guess,
                        double previous_gs_voltage,
                        double previous_gd_voltage,
                        double conductance_scale) noexcept {
    const auto drain = element.positive_node >= 0 ? guess[static_cast<std::size_t>(element.positive_node)] : 0.0;
    const auto gate = element.mosfet_gate_node >= 0 ? guess[static_cast<std::size_t>(element.mosfet_gate_node)] : 0.0;
    const auto source = element.negative_node >= 0 ? guess[static_cast<std::size_t>(element.negative_node)] : 0.0;
    const auto evaluation = EvaluateNonlinearMosfetDynamic(
        element.nonlinear_mosfet_dynamic_model, drain, gate, source,
        previous_gs_voltage, previous_gd_voltage, conductance_scale);
    if (!evaluation.Succeeded()) {
        StampConductance(matrix, size, element.positive_node, element.negative_node,
                         std::numeric_limits<double>::quiet_NaN());
        return;
    }
    const std::int32_t nodes[3] = {element.positive_node, element.mosfet_gate_node, element.negative_node};
    const double voltages[3] = {drain, gate, source};
    for (std::size_t row = 0; row < 3U; ++row) {
        if (nodes[row] < 0) continue;
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

void StampVariMu(std::vector<double>& matrix, std::vector<double>& rhs, std::uint32_t size,
                 const MnaPreparedCircuit::Element& element, const std::vector<double>& guess) noexcept {
    const auto voltage = [&guess](std::int32_t node) {
        return node >= 0 ? guess[static_cast<std::size_t>(node)] : 0.0;
    };
    const double plate = voltage(element.positive_node);
    const double grid = voltage(element.vari_mu_grid_node);
    const double cathode = voltage(element.negative_node);
    const auto evaluation = EvaluateNonlinearVariMu(
        element.nonlinear_vari_mu_model, plate, grid, cathode);
    if (!evaluation.Succeeded()) {
        StampConductance(matrix, size, element.positive_node, element.negative_node,
                         std::numeric_limits<double>::quiet_NaN());
        return;
    }
    const std::int32_t nodes[3] = {
        element.positive_node, element.vari_mu_grid_node, element.negative_node};
    const double voltages[3] = {plate, grid, cathode};
    for (std::size_t row = 0; row < 3U; ++row) {
        if (nodes[row] < 0) continue;
        double equivalent_current = evaluation.currents[row];
        for (std::size_t column = 0; column < 3U; ++column) {
            equivalent_current -= evaluation.jacobian[row][column] * voltages[column];
            if (nodes[column] >= 0) {
                matrix[static_cast<std::size_t>(nodes[row]) * size +
                       static_cast<std::size_t>(nodes[column])] += evaluation.jacobian[row][column];
            }
        }
        rhs[static_cast<std::size_t>(nodes[row])] -= equivalent_current;
    }
}

void StampBeamTetrode(std::vector<double>& matrix, std::vector<double>& rhs, std::uint32_t size,
                      const MnaPreparedCircuit::Element& element, const std::vector<double>& guess) noexcept {
    const auto voltage = [&guess](std::int32_t node) { return node >= 0 ? guess[static_cast<std::size_t>(node)] : 0.0; };
    const double plate = voltage(element.positive_node);
    const double screen = voltage(element.beam_tetrode_screen_node);
    const double grid = voltage(element.beam_tetrode_grid_node);
    const double cathode = voltage(element.negative_node);
    const auto evaluation = EvaluateNonlinearBeamTetrode(
        element.nonlinear_beam_tetrode_model, plate, screen, grid, cathode);
    if (!evaluation.Succeeded()) {
        StampConductance(matrix, size, element.positive_node, element.negative_node,
                         std::numeric_limits<double>::quiet_NaN());
        return;
    }
    const std::int32_t nodes[4] = {element.positive_node, element.beam_tetrode_screen_node,
                                   element.beam_tetrode_grid_node, element.negative_node};
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
                const std::vector<double>& guess,
                double conductance_scale,
                double previous_voltage) noexcept {
    const auto positive = element.positive_node >= 0 ? guess[static_cast<std::size_t>(element.positive_node)] : 0.0;
    const auto negative = element.negative_node >= 0 ? guess[static_cast<std::size_t>(element.negative_node)] : 0.0;
    const auto voltage = positive - negative;
    const auto evaluation = EvaluateNonlinearDiodeDynamic(
        element.nonlinear_diode_dynamic_model, voltage, previous_voltage, conductance_scale);
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

std::size_t MnaPreparedCircuit::ControlCount() const noexcept {
    return controls_.size();
}

std::optional<std::size_t> MnaPreparedCircuit::FindControl(std::string_view parameter_id) const noexcept {
    for (std::size_t index = 0; index < controls_.size(); ++index) {
        if (controls_[index].parameter_id == parameter_id) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<MnaPreparedCircuit::ControlRange> MnaPreparedCircuit::GetControlRange(
    std::size_t control_index) const noexcept {
    if (control_index >= controls_.size()) {
        return std::nullopt;
    }
    return ControlRange{.minimum = controls_[control_index].minimum,
                        .maximum = controls_[control_index].maximum};
}

bool MnaPreparedCircuit::SetControlTarget(std::size_t control_index, double value) noexcept {
    if (control_index >= controls_.size() || !std::isfinite(value)) {
        return false;
    }
    auto& control = controls_[control_index];
    if (value < control.minimum || value > control.maximum) {
        return false;
    }
    control.target = value;
    return true;
}

std::optional<double> MnaPreparedCircuit::ControlValue(std::size_t control_index) const noexcept {
    if (control_index >= controls_.size()) {
        return std::nullopt;
    }
    return controls_[control_index].current;
}

bool MnaPreparedCircuit::ApplyControl(Control& control) noexcept {
    if (control.primary_element_index >= elements_.size() || !std::isfinite(control.current)) {
        return false;
    }
    auto& primary = elements_[control.primary_element_index];
    switch (control.kind) {
    case ControlKind::PotentiometerPosition: {
        if (control.secondary_element_index >= elements_.size() || control.maximum <= control.minimum) {
            return false;
        }
        const double normalized = (control.current - control.minimum) / (control.maximum - control.minimum);
        constexpr double kMinimumResistance = 1.0e-12;
        primary.value = std::max(control.scale_a * normalized, kMinimumResistance);
        elements_[control.secondary_element_index].value =
            std::max(control.scale_a * (1.0 - normalized), kMinimumResistance);
        return std::isfinite(primary.value) && std::isfinite(elements_[control.secondary_element_index].value);
    }
    case ControlKind::SwitchState:
        primary.value = control.scale_a + control.current * (control.scale_b - control.scale_a);
        return std::isfinite(primary.value) && primary.value > 0.0;
    case ControlKind::SupplyVoltage:
        primary.value = control.current;
        if (primary.dynamic_power_supply_state_index != kNoIndex) {
            primary.dynamic_power_supply_model.voltage = control.current;
        }
        return true;
    }
    return false;
}

bool MnaPreparedCircuit::AdvanceControls(double timestep_seconds) noexcept {
    if (!std::isfinite(timestep_seconds) || timestep_seconds <= 0.0) {
        return false;
    }
    for (auto& control : controls_) {
        if (control.smoothing_seconds == 0.0) {
            control.current = control.target;
        } else {
            const double ratio = timestep_seconds / control.smoothing_seconds;
            const double alpha = ratio / (1.0 + ratio);
            control.current += alpha * (control.target - control.current);
        }
        control.current = std::clamp(control.current, control.minimum, control.maximum);
        if (!ApplyControl(control)) {
            return false;
        }
    }
    return true;
}

void MnaPreparedCircuit::Reset() noexcept {
    for (auto& control : controls_) {
        control.current = control.initial;
        control.target = control.initial;
        static_cast<void>(ApplyControl(control));
    }
    for (auto& history : capacitor_history_) {
        history = 0.0;
    }
    std::fill(bjt_be_history_.begin(), bjt_be_history_.end(), 0.0);
    std::fill(bjt_bc_history_.begin(), bjt_bc_history_.end(), 0.0);
    std::fill(jfet_gs_history_.begin(), jfet_gs_history_.end(), 0.0);
    std::fill(jfet_gd_history_.begin(), jfet_gd_history_.end(), 0.0);
    std::fill(mosfet_gs_history_.begin(), mosfet_gs_history_.end(), 0.0);
    std::fill(mosfet_gd_history_.begin(), mosfet_gd_history_.end(), 0.0);
    std::fill(opamp_dynamic_history_.begin(), opamp_dynamic_history_.end(), 0.0);
    std::fill(dynamic_power_supply_states_.begin(), dynamic_power_supply_states_.end(),
              DynamicPowerSupplyState{});
    std::fill(dynamic_speaker_load_states_.begin(), dynamic_speaker_load_states_.end(),
              DynamicSpeakerLoadState{});
    std::fill(dynamic_relay_states_.begin(), dynamic_relay_states_.end(), DynamicRelayState{});
    std::fill(optocoupler_illumination_states_.begin(), optocoupler_illumination_states_.end(), 0.0);
    for (const auto& element : elements_) {
        if (element.history_index != kNoIndex) {
            capacitor_history_[element.history_index] = element.initial;
        }
        if (element.inductor_history_index != kNoIndex) {
            inductor_history_[element.inductor_history_index] = element.initial;
        }
        if (element.secondary_inductor_history_index != kNoIndex) {
            inductor_history_[element.secondary_inductor_history_index] = element.secondary_initial;
        }
        if (element.transformer_core_history_index != kNoIndex) {
            transformer_core_history_[element.transformer_core_history_index] = {
                .flux = element.nonlinear_transformer_core_model.remanent_flux,
                .hysteresis = element.nonlinear_transformer_core_model.remanent_flux};
        }
        if (element.bjt_be_history_index != kNoIndex) {
            const double polarity = element.nonlinear_bjt_dynamic_model.static_model.polarity == BjtPolarity::Npn ? 1.0 : -1.0;
            bjt_be_history_[element.bjt_be_history_index] = polarity * element.initial;
        }
        if (element.bjt_bc_history_index != kNoIndex) {
            const double polarity = element.nonlinear_bjt_dynamic_model.static_model.polarity == BjtPolarity::Npn ? 1.0 : -1.0;
            bjt_bc_history_[element.bjt_bc_history_index] = polarity * element.initial;
        }
        if (element.jfet_gs_history_index != kNoIndex) {
            const double polarity = element.nonlinear_jfet_dynamic_model.static_model.polarity == JfetPolarity::NChannel ? 1.0 : -1.0;
            jfet_gs_history_[element.jfet_gs_history_index] = polarity * element.initial;
        }
        if (element.jfet_gd_history_index != kNoIndex) {
            const double polarity = element.nonlinear_jfet_dynamic_model.static_model.polarity == JfetPolarity::NChannel ? 1.0 : -1.0;
            jfet_gd_history_[element.jfet_gd_history_index] = polarity * element.initial;
        }
        if (element.mosfet_gs_history_index != kNoIndex) {
            const double polarity = element.nonlinear_mosfet_dynamic_model.static_model.polarity == MosfetPolarity::NChannel ? 1.0 : -1.0;
            mosfet_gs_history_[element.mosfet_gs_history_index] = polarity * element.initial;
        }
        if (element.mosfet_gd_history_index != kNoIndex) {
            const double polarity = element.nonlinear_mosfet_dynamic_model.static_model.polarity == MosfetPolarity::NChannel ? 1.0 : -1.0;
            mosfet_gd_history_[element.mosfet_gd_history_index] = polarity * element.initial;
        }
        if (element.opamp_dynamic_history_index != kNoIndex) {
            opamp_dynamic_history_[element.opamp_dynamic_history_index] = element.initial;
        }
        if (element.dynamic_power_supply_state_index != kNoIndex) {
            dynamic_power_supply_states_[element.dynamic_power_supply_state_index] = {
                .sag = element.initial, .ripple_phase = 0.0};
        }
        if (element.dynamic_speaker_load_state_index != kNoIndex) {
            dynamic_speaker_load_states_[element.dynamic_speaker_load_state_index] =
                element.dynamic_speaker_load_initial_state;
        }
        if (element.dynamic_relay_state_index != kNoIndex) {
            dynamic_relay_states_[element.dynamic_relay_state_index] = element.dynamic_relay_initial_state;
        }
        if (element.optocoupler_state_index != kNoIndex) {
            optocoupler_illumination_states_[element.optocoupler_state_index] = element.initial;
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
        if (element.type == CircuitComponentType::BeamTetrode && element.beam_tetrode_grid_node >= 0) {
            solution_[element.beam_tetrode_grid_node] = element.initial;
        }
        if (element.type == CircuitComponentType::VariMu && element.vari_mu_grid_node >= 0) {
            solution_[element.vari_mu_grid_node] = element.initial;
        }
    }
}

MnaSolveResult MnaPreparedCircuit::Solve(double input_voltage) noexcept {
    if (!std::isfinite(input_voltage)) {
        return {.error = MnaSolveError::NonFinite};
    }
    if (input_nodes_.empty()) {
        return Solve(std::span<const double>{});
    }
    if (input_nodes_.size() != 1U) {
        return {.error = MnaSolveError::InvalidInputCount};
    }
    return Solve(std::span<const double>(&input_voltage, 1U));
}

MnaSolveResult MnaPreparedCircuit::Solve(std::span<const double> input_voltages) noexcept {
    if (!prepared_) {
        return {.error = MnaSolveError::NotPrepared};
    }
    if (input_voltages.size() != input_nodes_.size()) {
        return {.error = MnaSolveError::InvalidInputCount};
    }
    for (const auto input_voltage : input_voltages) {
        if (!std::isfinite(input_voltage)) {
            return {.error = MnaSolveError::NonFinite};
        }
    }
    MnaSolveResult result;
    const double conductance_scale = sample_rate_hz_ * static_cast<double>(oversampling_);
    for (std::uint32_t substep = 0; substep < oversampling_; ++substep) {
        if (!AdvanceControls(1.0 / conductance_scale)) {
            return {.error = MnaSolveError::NonFinite};
        }
        const auto has_nonlinearity = std::any_of(elements_.begin(), elements_.end(), [](const auto& element) {
            return element.type == CircuitComponentType::Diode || element.type == CircuitComponentType::Bjt ||
                   element.type == CircuitComponentType::Jfet || element.type == CircuitComponentType::Mosfet ||
                   element.type == CircuitComponentType::OpAmp || element.type == CircuitComponentType::Ota ||
                   element.type == CircuitComponentType::Triode || element.type == CircuitComponentType::Pentode ||
                   element.type == CircuitComponentType::BeamTetrode ||
                   element.type == CircuitComponentType::VariMu ||
                   element.transformer_core_history_index != kNoIndex ||
                   element.dynamic_power_supply_state_index != kNoIndex ||
                   element.dynamic_speaker_load_state_index != kNoIndex ||
                   element.dynamic_relay_state_index != kNoIndex ||
                   element.optocoupler_state_index != kNoIndex ||
                   element.controlled_has_limits;
        });
        bool converged = !has_nonlinearity;
        for (std::uint32_t iteration = 0; iteration < max_iterations_; ++iteration) {
            ++result.iteration_count;
            result.maximum_iteration_count = std::max(result.maximum_iteration_count, iteration + 1U);
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
                case CircuitComponentType::SpeakerLoad:
                    StampDynamicSpeakerLoad(
                        matrix_, rhs_, matrix_size_, element, solution_,
                        dynamic_speaker_load_states_[element.dynamic_speaker_load_state_index],
                        1.0 / conductance_scale);
                    break;
                case CircuitComponentType::Relay:
                    StampDynamicRelay(matrix_, rhs_, matrix_size_, element, solution_,
                        dynamic_relay_states_[element.dynamic_relay_state_index],
                        1.0 / conductance_scale);
                    break;
                case CircuitComponentType::Optocoupler:
                    StampOptocoupler(matrix_, rhs_, matrix_size_, element, solution_,
                        optocoupler_illumination_states_[element.optocoupler_state_index],
                        1.0 / conductance_scale);
                    break;
                case CircuitComponentType::CoupledInductor: {
                    const auto primary_previous = inductor_history_[element.inductor_history_index];
                    const auto secondary_previous = inductor_history_[element.secondary_inductor_history_index];
                    if (element.transformer_core_history_index != kNoIndex) {
                        const auto primary_guess = solution_[element.branch_index];
                        const auto secondary_guess = solution_[element.secondary_branch_index];
                        const auto& state = transformer_core_history_[element.transformer_core_history_index];
                        StampNonlinearCoupledInductor(matrix_, rhs_, matrix_size_, element, conductance_scale,
                            primary_previous, secondary_previous, primary_guess, secondary_guess,
                            state.flux, state.hysteresis);
                    } else {
                        StampCoupledInductor(matrix_, rhs_, matrix_size_, element, conductance_scale,
                            primary_previous, secondary_previous);
                    }
                    break;
                }
                case CircuitComponentType::VoltageSource:
                    if (element.dynamic_power_supply_state_index != kNoIndex) {
                        StampDynamicPowerSupply(
                            matrix_, rhs_, matrix_size_, element, newton_previous_,
                            dynamic_power_supply_states_[element.dynamic_power_supply_state_index],
                            1.0 / conductance_scale);
                    } else {
                        StampVoltageSource(matrix_, rhs_, matrix_size_, element.positive_node, element.negative_node,
                                           element.branch_index, element.value);
                    }
                    break;
                case CircuitComponentType::Diode:
                    StampDiode(matrix_, rhs_, matrix_size_, element, newton_previous_, conductance_scale,
                               capacitor_history_[element.history_index]);
                    break;
                case CircuitComponentType::Bjt: {
                    const double previous_be = element.bjt_be_history_index != kNoIndex
                        ? bjt_be_history_[element.bjt_be_history_index] : 0.0;
                    const double previous_bc = element.bjt_bc_history_index != kNoIndex
                        ? bjt_bc_history_[element.bjt_bc_history_index] : 0.0;
                    StampBjt(matrix_, rhs_, matrix_size_, element, newton_previous_, conductance_scale,
                             previous_be, previous_bc);
                    break;
                }
                case CircuitComponentType::Jfet: {
                    const double previous_gs = element.jfet_gs_history_index != kNoIndex
                        ? jfet_gs_history_[element.jfet_gs_history_index] : 0.0;
                    const double previous_gd = element.jfet_gd_history_index != kNoIndex
                        ? jfet_gd_history_[element.jfet_gd_history_index] : 0.0;
                    StampJfet(matrix_, rhs_, matrix_size_, element, newton_previous_, conductance_scale,
                              previous_gs, previous_gd);
                    break;
                }
                case CircuitComponentType::Mosfet:
                    if (element.mosfet_gs_history_index != kNoIndex) {
                        const auto previous_gs = mosfet_gs_history_[element.mosfet_gs_history_index];
                        const auto previous_gd = mosfet_gd_history_[element.mosfet_gd_history_index];
                        StampMosfetDynamic(matrix_, rhs_, matrix_size_, element, newton_previous_,
                                           previous_gs, previous_gd, conductance_scale);
                    } else {
                        StampMosfet(matrix_, rhs_, matrix_size_, element, newton_previous_);
                    }
                    break;
                case CircuitComponentType::Vccs:
                    StampVccs(matrix_, rhs_, matrix_size_, element, newton_previous_);
                    break;
                case CircuitComponentType::IdealOpAmp:
                    StampIdealOpAmp(matrix_, rhs_, matrix_size_, element, newton_previous_);
                    break;
                case CircuitComponentType::OpAmp:
                    if (element.opamp_dynamic_history_index != kNoIndex) {
                        StampDynamicOpAmp(matrix_, rhs_, matrix_size_, element, newton_previous_,
                                          opamp_dynamic_history_[element.opamp_dynamic_history_index],
                                          1.0 / conductance_scale);
                    } else {
                        StampOpAmp(matrix_, rhs_, matrix_size_, element, newton_previous_);
                    }
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
                case CircuitComponentType::BeamTetrode:
                    StampBeamTetrode(matrix_, rhs_, matrix_size_, element, newton_previous_);
                    break;
                case CircuitComponentType::VariMu:
                    StampVariMu(matrix_, rhs_, matrix_size_, element, newton_previous_);
                    break;
                case CircuitComponentType::Potentiometer:
                case CircuitComponentType::Switch:
                    return {.error = MnaSolveError::NonFinite};
                }
            }
            for (std::size_t input_index = 0; input_index < input_nodes_.size(); ++input_index) {
                StampVoltageSource(matrix_, rhs_, matrix_size_, input_nodes_[input_index], -1,
                                   input_branches_[input_index], input_voltages[input_index]);
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
                if (element.type == CircuitComponentType::BeamTetrode) {
                    const auto old_p = element.positive_node >= 0 ? newton_previous_[element.positive_node] : 0.0;
                    const auto old_s = element.beam_tetrode_screen_node >= 0 ? newton_previous_[element.beam_tetrode_screen_node] : 0.0;
                    const auto old_g = element.beam_tetrode_grid_node >= 0 ? newton_previous_[element.beam_tetrode_grid_node] : 0.0;
                    const auto old_k = element.negative_node >= 0 ? newton_previous_[element.negative_node] : 0.0;
                    const auto new_p = element.positive_node >= 0 ? solution_[element.positive_node] : 0.0;
                    const auto new_s = element.beam_tetrode_screen_node >= 0 ? solution_[element.beam_tetrode_screen_node] : 0.0;
                    const auto new_g = element.beam_tetrode_grid_node >= 0 ? solution_[element.beam_tetrode_grid_node] : 0.0;
                    const auto new_k = element.negative_node >= 0 ? solution_[element.negative_node] : 0.0;
                    const double largest = std::max({std::fabs((new_p-new_k)-(old_p-old_k)),
                        std::fabs((new_s-new_k)-(old_s-old_k)), std::fabs((new_g-new_k)-(old_g-old_k))});
                    if (largest > element.nonlinear_beam_tetrode_model.max_step && std::isfinite(largest)) {
                        nonlinear_step_scale = std::min(nonlinear_step_scale,
                            element.nonlinear_beam_tetrode_model.max_step / largest);
                    }
                }
                if (element.type == CircuitComponentType::VariMu) {
                    const auto old_p = element.positive_node >= 0 ? newton_previous_[element.positive_node] : 0.0;
                    const auto old_g = element.vari_mu_grid_node >= 0 ? newton_previous_[element.vari_mu_grid_node] : 0.0;
                    const auto old_k = element.negative_node >= 0 ? newton_previous_[element.negative_node] : 0.0;
                    const auto new_p = element.positive_node >= 0 ? solution_[element.positive_node] : 0.0;
                    const auto new_g = element.vari_mu_grid_node >= 0 ? solution_[element.vari_mu_grid_node] : 0.0;
                    const auto new_k = element.negative_node >= 0 ? solution_[element.negative_node] : 0.0;
                    const double largest = std::max(
                        std::fabs((new_p - new_k) - (old_p - old_k)),
                        std::fabs((new_g - new_k) - (old_g - old_k)));
                    if (largest > element.nonlinear_vari_mu_model.max_step && std::isfinite(largest)) {
                        nonlinear_step_scale = std::min(
                            nonlinear_step_scale, element.nonlinear_vari_mu_model.max_step / largest);
                    }
                }
                if (element.transformer_core_history_index != kNoIndex) {
                    const double old_primary = newton_previous_[element.branch_index];
                    const double old_secondary = newton_previous_[element.secondary_branch_index];
                    const double new_primary = solution_[element.branch_index];
                    const double new_secondary = solution_[element.secondary_branch_index];
                    const double current_step = std::fabs(
                        (new_primary + new_secondary / element.coupled_inductor_model.turns_ratio) -
                        (old_primary + old_secondary / element.coupled_inductor_model.turns_ratio));
                    if (current_step > element.nonlinear_transformer_core_model.max_step &&
                        std::isfinite(current_step)) {
                        nonlinear_step_scale = std::min(
                            nonlinear_step_scale,
                            element.nonlinear_transformer_core_model.max_step / current_step);
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
            result.error = MnaSolveError::NonConvergent;
            return result;
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
            if (element.secondary_inductor_history_index != kNoIndex) {
                inductor_history_[element.secondary_inductor_history_index] =
                    solution_[element.secondary_branch_index];
            }
            if (element.transformer_core_history_index != kNoIndex) {
                const double primary = solution_[element.branch_index];
                const double secondary = solution_[element.secondary_branch_index];
                auto& state = transformer_core_history_[element.transformer_core_history_index];
                if (!AdvanceNonlinearTransformerCoreState(
                        element.nonlinear_transformer_core_model,
                        primary + secondary / element.coupled_inductor_model.turns_ratio,
                        1.0 / conductance_scale, state)) {
                    return {.error = MnaSolveError::NonFinite};
                }
            }
            if (element.bjt_be_history_index != kNoIndex) {
                const double polarity = element.nonlinear_bjt_dynamic_model.static_model.polarity == BjtPolarity::Npn ? 1.0 : -1.0;
                const double base = element.bjt_base_node >= 0 ? solution_[element.bjt_base_node] : 0.0;
                const double emitter = element.negative_node >= 0 ? solution_[element.negative_node] : 0.0;
                bjt_be_history_[element.bjt_be_history_index] = polarity * (base - emitter);
            }
            if (element.bjt_bc_history_index != kNoIndex) {
                const double polarity = element.nonlinear_bjt_dynamic_model.static_model.polarity == BjtPolarity::Npn ? 1.0 : -1.0;
                const double base = element.bjt_base_node >= 0 ? solution_[element.bjt_base_node] : 0.0;
                const double collector = element.positive_node >= 0 ? solution_[element.positive_node] : 0.0;
                bjt_bc_history_[element.bjt_bc_history_index] = polarity * (base - collector);
            }
            if (element.jfet_gs_history_index != kNoIndex) {
                const double polarity = element.nonlinear_jfet_dynamic_model.static_model.polarity == JfetPolarity::NChannel ? 1.0 : -1.0;
                const double gate = element.jfet_gate_node >= 0 ? solution_[element.jfet_gate_node] : 0.0;
                const double source = element.negative_node >= 0 ? solution_[element.negative_node] : 0.0;
                jfet_gs_history_[element.jfet_gs_history_index] = polarity * (gate - source);
            }
            if (element.jfet_gd_history_index != kNoIndex) {
                const double polarity = element.nonlinear_jfet_dynamic_model.static_model.polarity == JfetPolarity::NChannel ? 1.0 : -1.0;
                const double gate = element.jfet_gate_node >= 0 ? solution_[element.jfet_gate_node] : 0.0;
                const double drain = element.positive_node >= 0 ? solution_[element.positive_node] : 0.0;
                jfet_gd_history_[element.jfet_gd_history_index] = polarity * (gate - drain);
            }
            if (element.mosfet_gs_history_index != kNoIndex) {
                const double polarity = element.nonlinear_mosfet_dynamic_model.static_model.polarity == MosfetPolarity::NChannel ? 1.0 : -1.0;
                const double gate = element.mosfet_gate_node >= 0 ? solution_[element.mosfet_gate_node] : 0.0;
                const double source = element.negative_node >= 0 ? solution_[element.negative_node] : 0.0;
                mosfet_gs_history_[element.mosfet_gs_history_index] = polarity * (gate - source);
            }
            if (element.mosfet_gd_history_index != kNoIndex) {
                const double polarity = element.nonlinear_mosfet_dynamic_model.static_model.polarity == MosfetPolarity::NChannel ? 1.0 : -1.0;
                const double gate = element.mosfet_gate_node >= 0 ? solution_[element.mosfet_gate_node] : 0.0;
                const double drain = element.positive_node >= 0 ? solution_[element.positive_node] : 0.0;
                mosfet_gd_history_[element.mosfet_gd_history_index] = polarity * (gate - drain);
            }
            if (element.opamp_dynamic_history_index != kNoIndex) {
                const double output_positive = element.positive_node >= 0 ? solution_[element.positive_node] : 0.0;
                const double output_negative = element.negative_node >= 0 ? solution_[element.negative_node] : 0.0;
                const double control_positive = element.control_positive_node >= 0 ? solution_[element.control_positive_node] : 0.0;
                const double control_negative = element.control_negative_node >= 0 ? solution_[element.control_negative_node] : 0.0;
                const auto state = EvaluateNonlinearOpAmpDynamic(
                    element.nonlinear_opamp_dynamic_model, output_positive, output_negative,
                    control_positive, control_negative,
                    opamp_dynamic_history_[element.opamp_dynamic_history_index],
                    1.0 / conductance_scale);
                if (!state.Succeeded()) return {.error = MnaSolveError::NonFinite};
                opamp_dynamic_history_[element.opamp_dynamic_history_index] = state.state_voltage;
            }
            if (element.dynamic_power_supply_state_index != kNoIndex) {
                const auto state = EvaluateDynamicPowerSupply(
                    element.dynamic_power_supply_model, solution_[element.branch_index],
                    dynamic_power_supply_states_[element.dynamic_power_supply_state_index],
                    1.0 / conductance_scale);
                if (!state.Succeeded()) return {.error = MnaSolveError::NonFinite};
                dynamic_power_supply_states_[element.dynamic_power_supply_state_index] = state.next_state;
            }
            if (element.dynamic_speaker_load_state_index != kNoIndex) {
                const auto state = EvaluateDynamicSpeakerLoad(
                    element.dynamic_speaker_load_model, solution_[element.branch_index],
                    dynamic_speaker_load_states_[element.dynamic_speaker_load_state_index],
                    1.0 / conductance_scale);
                if (!state.Succeeded()) return {.error = MnaSolveError::NonFinite};
                dynamic_speaker_load_states_[element.dynamic_speaker_load_state_index] = state.next_state;
            }
            if (element.dynamic_relay_state_index != kNoIndex) {
                const auto state = EvaluateDynamicRelay(element.dynamic_relay_model,
                    solution_[element.branch_index], dynamic_relay_states_[element.dynamic_relay_state_index],
                    1.0 / conductance_scale);
                if (!state.Succeeded()) return {.error = MnaSolveError::NonFinite};
                dynamic_relay_states_[element.dynamic_relay_state_index] = state.next_state;
            }
            if (element.optocoupler_state_index != kNoIndex) {
                const double emitter_positive = element.positive_node >= 0
                    ? solution_[static_cast<std::size_t>(element.positive_node)] : 0.0;
                const double emitter_negative = element.negative_node >= 0
                    ? solution_[static_cast<std::size_t>(element.negative_node)] : 0.0;
                GenericOptocouplerMnaEvaluation state{};
                const auto error = EvaluateGenericOptocouplerMna(
                    element.optocoupler_mna_model, emitter_positive - emitter_negative,
                    optocoupler_illumination_states_[element.optocoupler_state_index],
                    1.0 / conductance_scale, state);
                if (error != GenericOptocouplerError::None) {
                    return {.error = MnaSolveError::NonFinite};
                }
                optocoupler_illumination_states_[element.optocoupler_state_index] =
                    state.next_illumination;
            }
        }
        result.output_voltage = output_nodes_.empty() || output_nodes_[0] < 0
            ? 0.0 : solution_[output_nodes_[0]];
    }
    if (!std::isfinite(result.output_voltage)) {
        result.error = MnaSolveError::NonFinite;
    }
    return result;
}

std::size_t MnaPreparedCircuit::InputCount() const noexcept {
    return input_nodes_.size();
}

std::size_t MnaPreparedCircuit::OutputCount() const noexcept {
    return output_nodes_.size();
}

std::optional<double> MnaPreparedCircuit::OutputValue(std::size_t output_index) const noexcept {
    if (!prepared_ || output_index >= output_nodes_.size()) {
        return std::nullopt;
    }
    const auto node = output_nodes_[output_index];
    return node < 0 ? 0.0 : std::optional<double>(solution_[node]);
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
    std::vector<const CircuitPort*> input_ports;
    std::vector<const CircuitPort*> output_ports;
    for (const auto& port : circuit.ports) {
        (port.direction == CircuitPortDirection::Input ? input_ports : output_ports).push_back(&port);
    }
    if (output_ports.empty()) {
        return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
    }

    MnaPreparedCircuit prepared;
    for (const auto& component : circuit.components) {
        if (component.initial.has_value() && !std::isfinite(*component.initial)) {
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
        }
        if ((component.primary_initial_current.has_value() &&
             !std::isfinite(*component.primary_initial_current)) ||
            (component.secondary_initial_current.has_value() &&
             !std::isfinite(*component.secondary_initial_current))) {
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
        }
        if ((component.bjt_model.has_value() && component.type != CircuitComponentType::Bjt) ||
            (component.jfet_model.has_value() && component.type != CircuitComponentType::Jfet) ||
            (component.mosfet_model.has_value() && component.type != CircuitComponentType::Mosfet) ||
            (component.amplifier_model.has_value() && component.type != CircuitComponentType::OpAmp &&
             component.type != CircuitComponentType::Ota) ||
            (component.triode_model.has_value() && component.type != CircuitComponentType::Triode) ||
            (component.pentode_model.has_value() && component.type != CircuitComponentType::Pentode) ||
            (component.beam_tetrode_model.has_value() && component.type != CircuitComponentType::BeamTetrode) ||
            (component.vari_mu_model.has_value() && component.type != CircuitComponentType::VariMu) ||
            (component.coupled_inductor_model.has_value() &&
             component.type != CircuitComponentType::CoupledInductor) ||
            (component.speaker_load_model.has_value() &&
             component.type != CircuitComponentType::SpeakerLoad) ||
            (component.relay_model.has_value() && component.type != CircuitComponentType::Relay) ||
            (component.optocoupler_model.has_value() && component.type != CircuitComponentType::Optocoupler) ||
            ((component.primary_initial_current.has_value() || component.secondary_initial_current.has_value()) &&
             component.type != CircuitComponentType::CoupledInductor)) {
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
        }
        if (component.type != CircuitComponentType::Resistor && component.type != CircuitComponentType::Capacitor &&
            component.type != CircuitComponentType::Inductor &&
            component.type != CircuitComponentType::VoltageSource && component.type != CircuitComponentType::Diode &&
            component.type != CircuitComponentType::Potentiometer && component.type != CircuitComponentType::Vccs &&
            component.type != CircuitComponentType::IdealOpAmp && component.type != CircuitComponentType::OpAmp &&
            component.type != CircuitComponentType::Ota && component.type != CircuitComponentType::Bjt &&
            component.type != CircuitComponentType::Jfet && component.type != CircuitComponentType::Mosfet &&
            component.type != CircuitComponentType::Triode && component.type != CircuitComponentType::Pentode &&
            component.type != CircuitComponentType::BeamTetrode && component.type != CircuitComponentType::VariMu &&
            component.type != CircuitComponentType::CoupledInductor &&
            component.type != CircuitComponentType::SpeakerLoad && component.type != CircuitComponentType::Switch &&
            component.type != CircuitComponentType::Relay && component.type != CircuitComponentType::Optocoupler) {
            return {.circuit = std::nullopt, .error = MnaCompileError::UnsupportedComponent};
        }
        if (component.type == CircuitComponentType::Switch) {
            if (component.pins.size() != 2U || !component.switch_spec.has_value() || component.value.has_value() ||
                component.model.has_value() || component.potentiometer.has_value() ||
                component.switch_spec->states != std::vector<std::string>{"open", "closed"} ||
                component.switch_spec->initial_state > 1U ||
                !std::isfinite(component.switch_spec->open_resistance) ||
                !std::isfinite(component.switch_spec->closed_resistance) ||
                component.switch_spec->open_resistance <= 0.0 ||
                component.switch_spec->closed_resistance <= 0.0 ||
                component.switch_spec->open_resistance > 1.0e15 ||
                component.switch_spec->closed_resistance > 1.0e15) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
            }
        }
        if (component.type == CircuitComponentType::SpeakerLoad) {
            if (component.pins.size() != 2U ||
                (!component.speaker_load_model.has_value() && !component.model.has_value()) ||
                component.value.has_value() || component.tolerance.has_value() ||
                component.initial.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            if (component.model.has_value() && !component.speaker_load_model.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            const auto& item = *component.speaker_load_model;
            const DynamicSpeakerLoadModel model{
                item.voice_coil_resistance, item.voice_coil_inductance, item.force_factor,
                item.moving_mass, item.mechanical_resistance, item.compliance,
                item.nonlinear_stiffness, item.nonlinear_displacement_scale,
                item.maximum_current, item.maximum_velocity, item.maximum_displacement};
            if (item.kind != "electrodynamic" || item.model_version != 1U ||
                !ValidateDynamicSpeakerLoadModel(model) ||
                std::fabs(item.initial_current) > item.maximum_current ||
                std::fabs(item.initial_velocity) > item.maximum_velocity ||
                std::fabs(item.initial_displacement) > item.maximum_displacement) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
            }
        }
        if (component.type == CircuitComponentType::Relay) {
            if (!component.relay_model.has_value() ||
                component.value.has_value() || component.tolerance.has_value() || component.initial.has_value() ||
                component.model.has_value() || component.diode_model.has_value() ||
                component.potentiometer.has_value() || component.switch_spec.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            const auto& item = *component.relay_model;
            const DynamicRelayModel model{item.coil_resistance, item.coil_inductance,
                item.open_resistance, item.closed_resistance, item.pull_in_current,
                item.drop_out_current, item.operate_time_seconds, item.release_time_seconds,
                item.bounce_duration_seconds, item.bounce_frequency_hz, item.bounce_amplitude,
                item.maximum_coil_current};
            const bool legacy = item.kind == "generic-dynamic" && item.model_version == 1U &&
                                component.pins.size() == 4U;
            const bool routed = item.kind == "generic-multi-contact" && item.model_version == 2U &&
                                component.pins.size() == 2U + 3U * item.pole_count &&
                                ValidateDynamicRelayRoutingModel(
                                    {.pole_count = item.pole_count, .contact_gap = item.contact_gap});
            if ((!legacy && !routed) || !ValidateDynamicRelayModel(model)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
            }
        }
        if (component.type == CircuitComponentType::Optocoupler) {
            if (component.pins.size() != 4U || !component.optocoupler_model.has_value() ||
                component.value.has_value() || component.tolerance.has_value() ||
                component.initial.has_value() || component.model.has_value() ||
                component.diode_model.has_value() || component.potentiometer.has_value() ||
                component.controlled_source.has_value() || component.bjt_model.has_value() ||
                component.jfet_model.has_value() || component.mosfet_model.has_value() ||
                component.amplifier_model.has_value() || component.triode_model.has_value() ||
                component.pentode_model.has_value() || component.beam_tetrode_model.has_value() ||
                component.vari_mu_model.has_value() || component.coupled_inductor_model.has_value() ||
                component.speaker_load_model.has_value() || component.relay_model.has_value() ||
                component.switch_spec.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            const auto& item = *component.optocoupler_model;
            const GenericOptocouplerMnaModel model{
                .optical = {.dark_resistance_ohms = item.dark_resistance_ohms,
                    .lit_resistance_ohms = item.lit_resistance_ohms,
                    .emitter_threshold_volts = item.emitter_threshold_volts,
                    .emitter_scale_volts = item.emitter_scale_volts,
                    .response_exponent = item.response_exponent,
                    .attack_seconds = item.attack_seconds,
                    .release_seconds = item.release_seconds,
                    .maximum_drive_volts = item.maximum_drive_volts,
                    .initial_illumination = item.initial_illumination},
                .emitter_resistance_ohms = item.emitter_resistance_ohms};
            if (item.kind != "generic-electrical" || item.model_version != 1U ||
                !ValidateGenericOptocouplerMnaModel(model)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
            }
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
            const bool dynamic_bjt = bjt.kind == "generic-dynamic" && bjt.model_version == 2U;
            if ((!dynamic_bjt && (bjt.kind != "ebers-moll" || bjt.model_version != 1U)) ||
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
            if (!ValidateNonlinearBjtDynamicModel({
                    .static_model = {.polarity = bjt.polarity == "npn" ? BjtPolarity::Npn : BjtPolarity::Pnp,
                        .saturation_current = bjt.saturation_current, .forward_beta = bjt.forward_beta,
                        .reverse_beta = bjt.reverse_beta, .thermal_voltage = bjt.thermal_voltage,
                        .max_current = bjt.max_current, .max_step = bjt.max_step},
                    .early_voltage = bjt.early_voltage,
                    .be_junction_capacitance = bjt.be_junction_capacitance,
                    .bc_junction_capacitance = bjt.bc_junction_capacitance,
                    .be_junction_potential = bjt.be_junction_potential,
                    .bc_junction_potential = bjt.bc_junction_potential,
                    .be_grading_coefficient = bjt.be_grading_coefficient,
                    .bc_grading_coefficient = bjt.bc_grading_coefficient,
                    .temperature_celsius = bjt.temperature_celsius,
                    .leakage_temperature_coefficient = bjt.leakage_temperature_coefficient})) {
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
            const bool dynamic_jfet = jfet.kind == "generic-dynamic" && jfet.model_version == 2U;
            if ((!dynamic_jfet && (jfet.kind != "square-law" || jfet.model_version != 1U)) ||
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
            if (!ValidateNonlinearJfetDynamicModel({
                    .static_model = model,
                    .channel_length_modulation = jfet.channel_length_modulation,
                    .temperature_celsius = jfet.temperature_celsius,
                    .gate_saturation_current = jfet.gate_saturation_current,
                    .gate_thermal_voltage = jfet.gate_thermal_voltage,
                    .gate_junction_capacitance = jfet.gate_junction_capacitance,
                    .gate_junction_potential = jfet.gate_junction_potential,
                    .gate_grading_coefficient = jfet.gate_grading_coefficient,
                    .gate_leakage_temperature_coefficient = jfet.gate_leakage_temperature_coefficient})) {
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
            const bool dynamic_mosfet = mosfet.kind == "generic-dynamic" && mosfet.model_version == 2U;
            if ((!dynamic_mosfet && (mosfet.kind != "enhancement-square-law" || mosfet.model_version != 1U)) ||
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
            if (amplifier.kind == "generic-dynamic") {
                if (component.type != CircuitComponentType::OpAmp || amplifier.model_version != 2U ||
                    !ValidateNonlinearOpAmpDynamicModel({
                        .gain = amplifier.gain, .gain_bandwidth_hz = amplifier.gain_bandwidth_hz,
                        .slew_rate = amplifier.slew_rate, .input_resistance = amplifier.input_resistance,
                        .input_bias_current = amplifier.input_bias_current, .input_offset = amplifier.input_offset,
                        .output_resistance = amplifier.output_resistance, .output_current_limit = amplifier.output_current_limit,
                        .supply_minimum = amplifier.supply_minimum, .supply_maximum = amplifier.supply_maximum,
                        .minimum_output = amplifier.minimum_output, .maximum_output = amplifier.maximum_output,
                        .max_step = amplifier.max_step}) ||
                    (component.initial.has_value() && (*component.initial < amplifier.minimum_output ||
                                                       *component.initial > amplifier.maximum_output))) {
                    return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
                }
            } else {
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
        if (component.type == CircuitComponentType::BeamTetrode) {
            if (component.pins.size() != 4U ||
                (!component.beam_tetrode_model.has_value() && !component.model.has_value()) ||
                component.value.has_value() || component.tolerance.has_value() || component.diode_model.has_value() ||
                component.potentiometer.has_value() || component.controlled_source.has_value() ||
                component.bjt_model.has_value() || component.jfet_model.has_value() || component.mosfet_model.has_value() ||
                component.amplifier_model.has_value() || component.triode_model.has_value() ||
                component.pentode_model.has_value() || component.switch_spec.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            if (component.model.has_value() && !component.beam_tetrode_model.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            const auto& item = *component.beam_tetrode_model;
            const NonlinearBeamTetrodeModel model{item.screen_amplification_factor, item.perveance,
                item.knee_voltage, item.screen_current_fraction, item.max_current, item.max_step};
            if (item.kind != "beam-capture" || item.model_version != 1U ||
                !ValidateNonlinearBeamTetrodeModel(model)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
            }
        }
        if (component.type == CircuitComponentType::VariMu) {
            if (component.pins.size() != 3U ||
                (!component.vari_mu_model.has_value() && !component.model.has_value()) ||
                component.value.has_value() || component.tolerance.has_value() ||
                component.diode_model.has_value() || component.potentiometer.has_value() ||
                component.controlled_source.has_value() || component.bjt_model.has_value() ||
                component.jfet_model.has_value() || component.mosfet_model.has_value() ||
                component.amplifier_model.has_value() || component.triode_model.has_value() ||
                component.pentode_model.has_value() || component.beam_tetrode_model.has_value() ||
                component.switch_spec.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            if (component.model.has_value() && !component.vari_mu_model.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            const auto& item = *component.vari_mu_model;
            const NonlinearVariMuModel model{item.amplification_factor, item.perveance,
                item.remote_cutoff_coefficient, item.max_current, item.max_step};
            if (item.kind != "remote-cutoff" || item.model_version != 1U ||
                !ValidateNonlinearVariMuModel(model)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
            }
        }
        if (component.type == CircuitComponentType::CoupledInductor) {
            if (component.pins.size() != 4U ||
                (!component.coupled_inductor_model.has_value() && !component.model.has_value()) ||
                component.value.has_value() || component.tolerance.has_value() || component.initial.has_value() ||
                component.diode_model.has_value() || component.potentiometer.has_value() ||
                component.controlled_source.has_value() || component.bjt_model.has_value() ||
                component.jfet_model.has_value() || component.mosfet_model.has_value() ||
                component.amplifier_model.has_value() || component.triode_model.has_value() ||
                component.pentode_model.has_value() || component.beam_tetrode_model.has_value() ||
                component.vari_mu_model.has_value() || component.switch_spec.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            if (component.model.has_value() && !component.coupled_inductor_model.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            const auto& item = *component.coupled_inductor_model;
            const CoupledInductorModel model{item.turns_ratio, item.magnetizing_inductance,
                item.primary_leakage_inductance, item.secondary_leakage_inductance,
                item.primary_resistance, item.secondary_resistance};
            const NonlinearTransformerCoreModel nonlinear_model{
                .magnetizing_inductance = item.magnetizing_inductance,
                .saturation_flux = item.saturation_flux,
                .hysteresis_width = item.hysteresis_width,
                .hysteresis_rate = item.hysteresis_rate,
                .remanent_flux = item.remanent_flux,
                .max_flux = item.max_flux,
                .max_current = item.max_current,
                .max_step = item.max_step};
            if (item.model_version != 1U ||
                (item.kind != "lossy-linear" && item.kind != "saturating-hysteretic") ||
                !ValidateCoupledInductorModel(model) ||
                (item.kind == "saturating-hysteretic" &&
                 !ValidateNonlinearTransformerCoreModel(nonlinear_model))) {
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

    for (const auto& supply : circuit.supplies) {
        if (!std::isfinite(supply.voltage) || std::fabs(supply.voltage) > 1.0e6 ||
            (supply.kind != "ideal" && supply.kind != "rectified-reservoir") ||
            supply.model_version != 1U) {
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
        }
        if (supply.kind == "rectified-reservoir" &&
            (!ValidateDynamicPowerSupplyModel({
                .voltage = supply.voltage, .source_resistance = supply.source_resistance,
                .sag_resistance = supply.sag_resistance,
                .recovery_time_seconds = supply.recovery_time_seconds,
                .ripple_amplitude = supply.ripple_amplitude,
                .ripple_frequency_hz = supply.ripple_frequency_hz,
                .maximum_sag = supply.maximum_sag,
                .maximum_load_current = supply.maximum_load_current}) ||
             !std::isfinite(supply.initial_sag) || supply.initial_sag < 0.0 ||
             supply.initial_sag > supply.maximum_sag)) {
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
        }
    }

    for (std::size_t index = 0; index < circuit.parameter_bindings.size(); ++index) {
        const auto& binding = circuit.parameter_bindings[index];
        if (binding.model_version != 1U || binding.parameter_id.empty() || binding.target_id.empty() ||
            !std::isfinite(binding.smoothing_seconds) || binding.smoothing_seconds < 0.0 ||
            binding.smoothing_seconds > 1.0e6 ||
            std::count_if(circuit.parameter_bindings.begin(), circuit.parameter_bindings.end(),
                [&binding](const CircuitParameterBinding& item) {
                    return item.parameter_id == binding.parameter_id;
                }) != 1 ||
            std::count_if(circuit.parameter_bindings.begin(), circuit.parameter_bindings.end(),
                [&binding](const CircuitParameterBinding& item) {
                    return item.target_id == binding.target_id && item.property == binding.property;
                }) != 1) {
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidValue};
        }
        const auto component = std::find_if(circuit.components.begin(), circuit.components.end(),
            [&binding](const CircuitComponent& item) { return item.id == binding.target_id; });
        const auto supply = std::find_if(circuit.supplies.begin(), circuit.supplies.end(),
            [&binding](const CircuitSupply& item) { return item.id == binding.target_id; });
        const bool matches =
            (binding.property == CircuitParameterBindingProperty::PotentiometerPosition &&
             component != circuit.components.end() && component->type == CircuitComponentType::Potentiometer) ||
            (binding.property == CircuitParameterBindingProperty::SwitchState &&
             component != circuit.components.end() && component->type == CircuitComponentType::Switch) ||
            (binding.property == CircuitParameterBindingProperty::SupplyVoltage && supply != circuit.supplies.end());
        if (!matches) {
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
        })) + static_cast<std::uint32_t>(circuit.supplies.size()) +
        static_cast<std::uint32_t>(input_ports.size());
    const auto inductor_count = static_cast<std::uint32_t>(std::count_if(
        circuit.components.begin(), circuit.components.end(), [](const CircuitComponent& component) {
            return component.type == CircuitComponentType::Inductor;
        }));
    const auto coupled_inductor_branch_count = 2U * static_cast<std::uint32_t>(std::count_if(
        circuit.components.begin(), circuit.components.end(), [](const CircuitComponent& component) {
            return component.type == CircuitComponentType::CoupledInductor;
        }));
    const auto speaker_branch_count = static_cast<std::uint32_t>(std::count_if(
        circuit.components.begin(), circuit.components.end(), [](const CircuitComponent& component) {
            return component.type == CircuitComponentType::SpeakerLoad;
        }));
    const auto relay_branch_count = static_cast<std::uint32_t>(std::count_if(
        circuit.components.begin(), circuit.components.end(), [](const CircuitComponent& component) {
            return component.type == CircuitComponentType::Relay;
        }));
    const auto controlled_voltage_source_count = static_cast<std::uint32_t>(std::count_if(
        circuit.components.begin(), circuit.components.end(), [](const CircuitComponent& component) {
            return component.type == CircuitComponentType::IdealOpAmp ||
                   (component.type == CircuitComponentType::OpAmp && component.amplifier_model.has_value() &&
                    component.amplifier_model->kind != "generic-dynamic");
        }));
    const auto branch_count = voltage_source_count + inductor_count + coupled_inductor_branch_count +
                              speaker_branch_count + relay_branch_count + controlled_voltage_source_count;
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
    prepared.output_nodes_.reserve(output_ports.size());
    for (const auto* port : output_ports) {
        const auto output_node = FindNode(circuit, port->node);
        if (output_node == -2) {
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
        }
        prepared.output_nodes_.push_back(output_node);
    }
    prepared.input_nodes_.reserve(input_ports.size());
    for (const auto* port : input_ports) {
        const auto input_node = FindNode(circuit, port->node);
        if (input_node < 0) {
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
        }
        prepared.input_nodes_.push_back(input_node);
    }
    std::uint32_t branch{};
    std::uint32_t history{};
    constexpr double kMinimumPotSegmentResistance = 1.0e-12;
    for (const auto& component : circuit.components) {
        const auto positive = FindNode(circuit, component.pins[0]);
        const bool is_three_terminal = component.type == CircuitComponentType::Bjt ||
            component.type == CircuitComponentType::Jfet || component.type == CircuitComponentType::Mosfet ||
            component.type == CircuitComponentType::Triode || component.type == CircuitComponentType::VariMu;
        const auto negative = FindNode(circuit, component.pins[
            is_three_terminal ? 2U :
                ((component.type == CircuitComponentType::Pentode ||
                  component.type == CircuitComponentType::BeamTetrode) ? 3U : 1U)]);
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
                                                    .max_step = bjt.max_step},
                                                .nonlinear_bjt_dynamic_model = {
                                                    .static_model = {
                                                        .polarity = bjt.polarity == "npn" ? BjtPolarity::Npn : BjtPolarity::Pnp,
                                                        .saturation_current = bjt.saturation_current,
                                                        .forward_beta = bjt.forward_beta,
                                                        .reverse_beta = bjt.reverse_beta,
                                                        .thermal_voltage = bjt.thermal_voltage,
                                                        .max_current = bjt.max_current,
                                                        .max_step = bjt.max_step},
                                                    .early_voltage = bjt.early_voltage,
                                                    .be_junction_capacitance = bjt.be_junction_capacitance,
                                                    .bc_junction_capacitance = bjt.bc_junction_capacitance,
                                                    .be_junction_potential = bjt.be_junction_potential,
                                                    .bc_junction_potential = bjt.bc_junction_potential,
                                                    .be_grading_coefficient = bjt.be_grading_coefficient,
                                                    .bc_grading_coefficient = bjt.bc_grading_coefficient,
                                                    .temperature_celsius = bjt.temperature_celsius,
                                                    .leakage_temperature_coefficient = bjt.leakage_temperature_coefficient}};
            if (!ValidateNonlinearBjtDynamicModel(element.nonlinear_bjt_dynamic_model)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            element.bjt_be_history_index = static_cast<std::uint32_t>(prepared.bjt_be_history_.size());
            prepared.bjt_be_history_.push_back(0.0);
            element.bjt_bc_history_index = static_cast<std::uint32_t>(prepared.bjt_bc_history_.size());
            prepared.bjt_bc_history_.push_back(0.0);
            prepared.elements_.push_back(element);
            continue;
        }
        if (component.type == CircuitComponentType::Relay) {
            const auto& item = *component.relay_model;
            MnaPreparedCircuit::Element element{};
            element.type = CircuitComponentType::Relay;
            element.positive_node = positive;
            element.negative_node = negative;
            element.branch_index = non_ground_nodes + branch++;
            element.dynamic_relay_model = {item.coil_resistance, item.coil_inductance,
                item.open_resistance, item.closed_resistance, item.pull_in_current,
                item.drop_out_current, item.operate_time_seconds, item.release_time_seconds,
                item.bounce_duration_seconds, item.bounce_frequency_hz, item.bounce_amplitude,
                item.maximum_coil_current};
            element.dynamic_relay_model_version = item.model_version;
            element.dynamic_relay_routing_model = {
                .pole_count = item.pole_count, .contact_gap = item.contact_gap};
            if (item.model_version == 1U) {
                const auto contact_positive = FindNode(circuit, component.pins[2]);
                const auto contact_negative = FindNode(circuit, component.pins[3]);
                if (contact_positive == -2 || contact_negative == -2 ||
                    contact_positive == contact_negative) {
                    return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
                }
                element.secondary_positive_node = contact_positive;
                element.secondary_negative_node = contact_negative;
            } else {
                for (std::uint32_t pole = 0; pole < item.pole_count; ++pole) {
                    const auto pin = 2U + 3U * pole;
                    const auto common = FindNode(circuit, component.pins[pin]);
                    const auto normally_closed = FindNode(circuit, component.pins[pin + 1U]);
                    const auto normally_open = FindNode(circuit, component.pins[pin + 2U]);
                    if (common == -2 || normally_closed == -2 || normally_open == -2 ||
                        common == normally_closed || common == normally_open ||
                        normally_closed == normally_open) {
                        return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
                    }
                    element.relay_common_nodes[pole] = common;
                    element.relay_normally_closed_nodes[pole] = normally_closed;
                    element.relay_normally_open_nodes[pole] = normally_open;
                }
            }
            element.dynamic_relay_initial_state = {.coil_current = 0.0,
                .position = item.initial_energized ? 1.0 : 0.0,
                .energized = item.initial_energized};
            element.dynamic_relay_state_index =
                static_cast<std::uint32_t>(prepared.dynamic_relay_states_.size());
            prepared.dynamic_relay_states_.push_back(element.dynamic_relay_initial_state);
            prepared.elements_.push_back(element);
            continue;
        }
        if (component.type == CircuitComponentType::Optocoupler) {
            const auto detector_positive = FindNode(circuit, component.pins[2]);
            const auto detector_negative = FindNode(circuit, component.pins[3]);
            if (detector_positive == -2 || detector_negative == -2 ||
                detector_positive == detector_negative) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
            }
            const auto& item = *component.optocoupler_model;
            MnaPreparedCircuit::Element element{};
            element.type = CircuitComponentType::Optocoupler;
            element.positive_node = positive;
            element.negative_node = negative;
            element.secondary_positive_node = detector_positive;
            element.secondary_negative_node = detector_negative;
            element.value = item.emitter_resistance_ohms;
            element.initial = item.initial_illumination;
            element.optocoupler_mna_model = {
                .optical = {.dark_resistance_ohms = item.dark_resistance_ohms,
                    .lit_resistance_ohms = item.lit_resistance_ohms,
                    .emitter_threshold_volts = item.emitter_threshold_volts,
                    .emitter_scale_volts = item.emitter_scale_volts,
                    .response_exponent = item.response_exponent,
                    .attack_seconds = item.attack_seconds,
                    .release_seconds = item.release_seconds,
                    .maximum_drive_volts = item.maximum_drive_volts,
                    .initial_illumination = item.initial_illumination},
                .emitter_resistance_ohms = item.emitter_resistance_ohms};
            element.optocoupler_state_index = static_cast<std::uint32_t>(
                prepared.optocoupler_illumination_states_.size());
            prepared.optocoupler_illumination_states_.push_back(item.initial_illumination);
            prepared.elements_.push_back(element);
            continue;
        }
        if (component.type == CircuitComponentType::CoupledInductor) {
            const auto secondary_positive = FindNode(circuit, component.pins[2]);
            const auto secondary_negative = FindNode(circuit, component.pins[3]);
            if (secondary_positive == -2 || secondary_negative == -2 ||
                secondary_positive == secondary_negative) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
            }
            const auto& item = *component.coupled_inductor_model;
            const CoupledInductorModel model{item.turns_ratio, item.magnetizing_inductance,
                item.primary_leakage_inductance, item.secondary_leakage_inductance,
                item.primary_resistance, item.secondary_resistance};
            const auto coefficients = PrepareCoupledInductor(model);
            if (!coefficients.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            MnaPreparedCircuit::Element element{};
            element.type = CircuitComponentType::CoupledInductor;
            element.positive_node = positive;
            element.negative_node = negative;
            element.secondary_positive_node = secondary_positive;
            element.secondary_negative_node = secondary_negative;
            element.branch_index = non_ground_nodes + branch++;
            element.secondary_branch_index = non_ground_nodes + branch++;
            element.inductor_history_index = static_cast<std::uint32_t>(prepared.inductor_history_.size());
            prepared.inductor_history_.push_back(0.0);
            element.secondary_inductor_history_index =
                static_cast<std::uint32_t>(prepared.inductor_history_.size());
            prepared.inductor_history_.push_back(0.0);
            element.initial = component.primary_initial_current.value_or(0.0);
            element.secondary_initial = component.secondary_initial_current.value_or(0.0);
            element.coupled_inductor_model = model;
            element.coupled_inductor_coefficients = *coefficients;
            if (item.kind == "saturating-hysteretic") {
                element.transformer_core_history_index =
                    static_cast<std::uint32_t>(prepared.transformer_core_history_.size());
                element.nonlinear_transformer_core_model = {
                    .magnetizing_inductance = item.magnetizing_inductance,
                    .saturation_flux = item.saturation_flux,
                    .hysteresis_width = item.hysteresis_width,
                    .hysteresis_rate = item.hysteresis_rate,
                    .remanent_flux = item.remanent_flux,
                    .max_flux = item.max_flux,
                    .max_current = item.max_current,
                    .max_step = item.max_step};
                prepared.transformer_core_history_.push_back({
                    .flux = item.remanent_flux, .hysteresis = item.remanent_flux});
            }
            prepared.elements_.push_back(element);
            continue;
        }
        if (component.type == CircuitComponentType::SpeakerLoad) {
            const auto& item = *component.speaker_load_model;
            MnaPreparedCircuit::Element element{};
            element.type = CircuitComponentType::SpeakerLoad;
            element.positive_node = positive;
            element.negative_node = negative;
            element.branch_index = non_ground_nodes + branch++;
            element.dynamic_speaker_load_model = {
                item.voice_coil_resistance, item.voice_coil_inductance, item.force_factor,
                item.moving_mass, item.mechanical_resistance, item.compliance,
                item.nonlinear_stiffness, item.nonlinear_displacement_scale,
                item.maximum_current, item.maximum_velocity, item.maximum_displacement};
            element.dynamic_speaker_load_initial_state = {
                item.initial_current, item.initial_velocity, item.initial_displacement};
            element.dynamic_speaker_load_state_index =
                static_cast<std::uint32_t>(prepared.dynamic_speaker_load_states_.size());
            prepared.dynamic_speaker_load_states_.push_back(
                element.dynamic_speaker_load_initial_state);
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
                                                    .max_step = jfet.max_step},
                                                .nonlinear_jfet_dynamic_model = {
                                                    .static_model = {
                                                        .polarity = jfet.polarity == "n-channel" ? JfetPolarity::NChannel : JfetPolarity::PChannel,
                                                        .pinch_off_voltage = jfet.pinch_off_voltage,
                                                        .transconductance_parameter = jfet.transconductance_parameter,
                                                        .max_current = jfet.max_current,
                                                        .max_step = jfet.max_step},
                                                    .channel_length_modulation = jfet.channel_length_modulation,
                                                    .temperature_celsius = jfet.temperature_celsius,
                                                    .gate_saturation_current = jfet.gate_saturation_current,
                                                    .gate_thermal_voltage = jfet.gate_thermal_voltage,
                                                    .gate_junction_capacitance = jfet.gate_junction_capacitance,
                                                    .gate_junction_potential = jfet.gate_junction_potential,
                                                    .gate_grading_coefficient = jfet.gate_grading_coefficient,
                                                    .gate_leakage_temperature_coefficient = jfet.gate_leakage_temperature_coefficient}};
            if (!ValidateNonlinearJfetDynamicModel(element.nonlinear_jfet_dynamic_model)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            element.jfet_gs_history_index = static_cast<std::uint32_t>(prepared.jfet_gs_history_.size());
            prepared.jfet_gs_history_.push_back(0.0);
            element.jfet_gd_history_index = static_cast<std::uint32_t>(prepared.jfet_gd_history_.size());
            prepared.jfet_gd_history_.push_back(0.0);
            prepared.elements_.push_back(element);
            continue;
        }
        if (component.type == CircuitComponentType::Mosfet) {
            const auto gate = FindNode(circuit, component.pins[1]);
            if (gate == -2 || gate == positive || gate == negative) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
            }
            const auto& mosfet = *component.mosfet_model;
            const bool dynamic_mosfet = mosfet.kind == "generic-dynamic" && mosfet.model_version == 2U;
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
            if (dynamic_mosfet) {
                element.nonlinear_mosfet_dynamic_model = {
                    .static_model = element.nonlinear_mosfet_model,
                    .channel_length_modulation = mosfet.channel_length_modulation,
                    .subthreshold_current = mosfet.subthreshold_current,
                    .subthreshold_slope_voltage = mosfet.subthreshold_slope_voltage,
                    .temperature_celsius = mosfet.temperature_celsius,
                    .body_diode_saturation_current = mosfet.body_diode_saturation_current,
                    .body_diode_thermal_voltage = mosfet.body_diode_thermal_voltage,
                    .gate_junction_capacitance = mosfet.gate_junction_capacitance,
                    .gate_junction_potential = mosfet.gate_junction_potential,
                    .gate_grading_coefficient = mosfet.gate_grading_coefficient};
                if (!ValidateNonlinearMosfetDynamicModel(element.nonlinear_mosfet_dynamic_model)) {
                    return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
                }
                element.mosfet_gs_history_index = static_cast<std::uint32_t>(prepared.mosfet_gs_history_.size());
                prepared.mosfet_gs_history_.push_back(0.0);
                element.mosfet_gd_history_index = static_cast<std::uint32_t>(prepared.mosfet_gd_history_.size());
                prepared.mosfet_gd_history_.push_back(0.0);
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
        if (component.type == CircuitComponentType::BeamTetrode) {
            const auto screen = FindNode(circuit, component.pins[1]);
            const auto grid = FindNode(circuit, component.pins[2]);
            if (screen == -2 || grid == -2 || screen == positive || screen == negative ||
                grid == positive || grid == screen || grid == negative) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
            }
            const auto& item = *component.beam_tetrode_model;
            MnaPreparedCircuit::Element element{};
            element.type = CircuitComponentType::BeamTetrode;
            element.positive_node = positive;
            element.negative_node = negative;
            element.beam_tetrode_screen_node = screen;
            element.beam_tetrode_grid_node = grid;
            element.branch_index = kNoIndex;
            element.initial = component.initial.value_or(0.0);
            element.nonlinear_beam_tetrode_model = {item.screen_amplification_factor, item.perveance,
                item.knee_voltage, item.screen_current_fraction, item.max_current, item.max_step};
            prepared.elements_.push_back(element);
            continue;
        }
        if (component.type == CircuitComponentType::VariMu) {
            const auto grid = FindNode(circuit, component.pins[1]);
            if (grid == -2 || grid == positive || grid == negative) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidTopology};
            }
            const auto& item = *component.vari_mu_model;
            MnaPreparedCircuit::Element element{};
            element.type = CircuitComponentType::VariMu;
            element.positive_node = positive;
            element.negative_node = negative;
            element.vari_mu_grid_node = grid;
            element.branch_index = kNoIndex;
            element.initial = component.initial.value_or(0.0);
            element.nonlinear_vari_mu_model = {item.amplification_factor, item.perveance,
                item.remote_cutoff_coefficient, item.max_current, item.max_step};
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
            const auto first_index = static_cast<std::uint32_t>(prepared.elements_.size());
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
            const auto binding = std::find_if(circuit.parameter_bindings.begin(), circuit.parameter_bindings.end(),
                [&component](const CircuitParameterBinding& item) {
                    return item.target_id == component.id &&
                           item.property == CircuitParameterBindingProperty::PotentiometerPosition;
                });
            if (binding != circuit.parameter_bindings.end()) {
                prepared.controls_.push_back({.kind = MnaPreparedCircuit::ControlKind::PotentiometerPosition,
                    .parameter_id = binding->parameter_id, .primary_element_index = first_index,
                    .secondary_element_index = first_index + 1U, .minimum = spec.minimum,
                    .maximum = spec.maximum, .initial = spec.position, .current = spec.position,
                    .target = spec.position, .smoothing_seconds = binding->smoothing_seconds,
                    .scale_a = total});
            }
            continue;
        }
        if (component.type == CircuitComponentType::Switch) {
            const auto& spec = *component.switch_spec;
            const auto element_index = static_cast<std::uint32_t>(prepared.elements_.size());
            const double initial_state = static_cast<double>(spec.initial_state);
            const double resistance = spec.open_resistance +
                initial_state * (spec.closed_resistance - spec.open_resistance);
            prepared.elements_.push_back({.type = CircuitComponentType::Resistor,
                .positive_node = positive, .negative_node = negative, .branch_index = kNoIndex,
                .history_index = kNoIndex, .inductor_history_index = kNoIndex,
                .value = resistance, .initial = 0.0});
            const auto binding = std::find_if(circuit.parameter_bindings.begin(), circuit.parameter_bindings.end(),
                [&component](const CircuitParameterBinding& item) {
                    return item.target_id == component.id &&
                           item.property == CircuitParameterBindingProperty::SwitchState;
                });
            if (binding != circuit.parameter_bindings.end()) {
                prepared.controls_.push_back({.kind = MnaPreparedCircuit::ControlKind::SwitchState,
                    .parameter_id = binding->parameter_id, .primary_element_index = element_index,
                    .minimum = 0.0, .maximum = 1.0, .initial = initial_state,
                    .current = initial_state, .target = initial_state,
                    .smoothing_seconds = binding->smoothing_seconds,
                    .scale_a = spec.open_resistance, .scale_b = spec.closed_resistance});
            }
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
            element.branch_index = component.type == CircuitComponentType::OpAmp && amplifier.kind != "generic-dynamic"
                ? non_ground_nodes + branch++ : kNoIndex;
            element.initial = component.initial.value_or(0.0);
            element.nonlinear_amplifier_model = {
                .kind = component.type == CircuitComponentType::OpAmp
                    ? AmplifierKind::OpAmp : AmplifierKind::Ota,
                .gain = amplifier.gain,
                .minimum_output = amplifier.minimum_output,
                .maximum_output = amplifier.maximum_output,
                .max_step = amplifier.max_step};
            if (amplifier.kind == "generic-dynamic") {
                element.opamp_dynamic_history_index = static_cast<std::uint32_t>(prepared.opamp_dynamic_history_.size());
                prepared.opamp_dynamic_history_.push_back(element.initial);
                element.nonlinear_opamp_dynamic_model = {
                    .gain = amplifier.gain, .gain_bandwidth_hz = amplifier.gain_bandwidth_hz,
                    .slew_rate = amplifier.slew_rate, .input_resistance = amplifier.input_resistance,
                    .input_bias_current = amplifier.input_bias_current, .input_offset = amplifier.input_offset,
                    .output_resistance = amplifier.output_resistance, .output_current_limit = amplifier.output_current_limit,
                    .supply_minimum = amplifier.supply_minimum, .supply_maximum = amplifier.supply_maximum,
                    .minimum_output = amplifier.minimum_output, .maximum_output = amplifier.maximum_output,
                    .max_step = amplifier.max_step};
                if (!ValidateNonlinearOpAmpDynamicModel(element.nonlinear_opamp_dynamic_model)) {
                    return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
                }
            } else if (!ValidateNonlinearAmplifierModel(element.nonlinear_amplifier_model)) {
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
            element.nonlinear_diode_dynamic_model = {
                .static_model = element.nonlinear_model,
                .junction_capacitance = component.diode_model->junction_capacitance,
                .junction_potential = component.diode_model->junction_potential,
                .grading_coefficient = component.diode_model->grading_coefficient,
                .temperature_celsius = component.diode_model->temperature_celsius,
                .leakage_current = component.diode_model->leakage_current,
                .leakage_temperature_coefficient =
                    component.diode_model->leakage_temperature_coefficient};
            if (!ValidateNonlinearDiodeDynamicModel(element.nonlinear_diode_dynamic_model)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
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
        MnaPreparedCircuit::Element element{};
        element.type = CircuitComponentType::VoltageSource;
        element.positive_node = positive;
        element.negative_node = negative;
        element.control_positive_node = -1;
        element.control_negative_node = -1;
        element.branch_index = non_ground_nodes + branch++;
        element.history_index = kNoIndex;
        element.inductor_history_index = kNoIndex;
        element.value = supply.voltage;
        if (supply.kind == "rectified-reservoir") {
            element.initial = supply.initial_sag;
            element.dynamic_power_supply_model = {
                .voltage = supply.voltage, .source_resistance = supply.source_resistance,
                .sag_resistance = supply.sag_resistance,
                .recovery_time_seconds = supply.recovery_time_seconds,
                .ripple_amplitude = supply.ripple_amplitude,
                .ripple_frequency_hz = supply.ripple_frequency_hz,
                .maximum_sag = supply.maximum_sag,
                .maximum_load_current = supply.maximum_load_current};
            element.dynamic_power_supply_state_index =
                static_cast<std::uint32_t>(prepared.dynamic_power_supply_states_.size());
            prepared.dynamic_power_supply_states_.push_back({.sag = supply.initial_sag});
        }
        const auto element_index = static_cast<std::uint32_t>(prepared.elements_.size());
        prepared.elements_.push_back(element);
        const auto binding = std::find_if(circuit.parameter_bindings.begin(), circuit.parameter_bindings.end(),
            [&supply](const CircuitParameterBinding& item) {
                return item.target_id == supply.id &&
                       item.property == CircuitParameterBindingProperty::SupplyVoltage;
            });
        if (binding != circuit.parameter_bindings.end()) {
            prepared.controls_.push_back({.kind = MnaPreparedCircuit::ControlKind::SupplyVoltage,
                .parameter_id = binding->parameter_id, .primary_element_index = element_index,
                .minimum = -1.0e6, .maximum = 1.0e6, .initial = supply.voltage,
                .current = supply.voltage, .target = supply.voltage,
                .smoothing_seconds = binding->smoothing_seconds});
        }
    }
    prepared.input_branches_.reserve(input_ports.size());
    for (std::size_t input_index = 0; input_index < input_ports.size(); ++input_index) {
        static_cast<void>(input_index);
        prepared.input_branches_.push_back(non_ground_nodes + branch++);
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
            component.type != CircuitComponentType::Triode && component.type != CircuitComponentType::Pentode &&
            component.type != CircuitComponentType::BeamTetrode && component.type != CircuitComponentType::VariMu &&
            component.type != CircuitComponentType::CoupledInductor &&
            component.type != CircuitComponentType::SpeakerLoad) {
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
            if (component.type == CircuitComponentType::BeamTetrode && component.beam_tetrode_model.has_value()) continue;
            if (component.type == CircuitComponentType::VariMu && component.vari_mu_model.has_value()) continue;
            if (component.type == CircuitComponentType::CoupledInductor &&
                component.coupled_inductor_model.has_value()) continue;
            if (component.type == CircuitComponentType::SpeakerLoad &&
                component.speaker_load_model.has_value()) continue;
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
        }
        if (component.type == CircuitComponentType::SpeakerLoad) {
            const auto result = model_database.Bind({*component.model, ModelKind::SpeakerLoad});
            if (!result.model.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            CircuitSpeakerLoadModel speaker_bound{};
            if (!BindAuditedSpeakerLoadModel(*result.model, speaker_bound)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            component.speaker_load_model = speaker_bound;
            continue;
        }
        if (component.type == CircuitComponentType::CoupledInductor) {
            const auto result = model_database.Bind({*component.model, ModelKind::TransformerCore});
            if (!result.model.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            const auto& record = *result.model;
            if (component.type == CircuitComponentType::OpAmp &&
                ModelParameter(record, "gain-bandwidth").has_value()) {
                CircuitAmplifierModel dynamic_bound{};
                if (!BindAuditedDynamicOpAmpModel(record, dynamic_bound)) {
                    return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
                }
                component.amplifier_model = dynamic_bound;
                continue;
            }
            const auto turns_ratio = ModelParameter(record, "turns-ratio");
            const auto magnetizing = ModelParameter(record, "magnetizing-inductance");
            const auto primary_leakage = ModelParameter(record, "primary-leakage-inductance");
            const auto secondary_leakage = ModelParameter(record, "secondary-leakage-inductance");
            const auto primary_resistance = ModelParameter(record, "primary-resistance");
            const auto secondary_resistance = ModelParameter(record, "secondary-resistance");
            if (!turns_ratio || !magnetizing || !primary_leakage || !secondary_leakage ||
                !primary_resistance || !secondary_resistance) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            const bool nonlinear = record.model_id ==
                "solorig.cleanroom.transformer-core.saturating-hysteretic.v1";
            component.coupled_inductor_model = CircuitCoupledInductorModel{
                .kind = nonlinear ? "saturating-hysteretic" : "lossy-linear",
                .model_version = 1U,
                .turns_ratio = *turns_ratio,
                .magnetizing_inductance = *magnetizing,
                .primary_leakage_inductance = *primary_leakage,
                .secondary_leakage_inductance = *secondary_leakage,
                .primary_resistance = *primary_resistance,
                .secondary_resistance = *secondary_resistance,
                .saturation_flux = ModelParameter(record, "saturation-flux").value_or(1.0),
                .hysteresis_width = ModelParameter(record, "hysteresis-width").value_or(0.1),
                .hysteresis_rate = ModelParameter(record, "hysteresis-rate").value_or(1000.0),
                .remanent_flux = ModelParameter(record, "remanent-flux").value_or(0.0),
                .max_flux = ModelParameter(record, "max-flux").value_or(2.0),
                .max_current = ModelParameter(record, "max-current").value_or(10.0),
                .max_step = ModelParameter(record, "max-step").value_or(1.0)};
            continue;
        }
        if (component.type == CircuitComponentType::Bjt) {
            const auto result = model_database.Bind({*component.model, ModelKind::Bjt});
            if (!result.model.has_value() || !result.model->polarity.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            CircuitBjtModel dynamic_bound{};
            if (!BindAuditedBjtModel(*result.model, dynamic_bound)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            component.bjt_model = dynamic_bound;
            continue;
        }
        if (component.type == CircuitComponentType::Jfet) {
            const auto result = model_database.Bind({*component.model, ModelKind::Jfet});
            if (!result.model.has_value() || !result.model->polarity.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            CircuitJfetModel dynamic_jfet_bound{};
            if (!BindAuditedJfetModel(*result.model, dynamic_jfet_bound)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            component.jfet_model = dynamic_jfet_bound;
            continue;
        }
        if (component.type == CircuitComponentType::Mosfet) {
            const auto result = model_database.Bind({*component.model, ModelKind::Mosfet});
            if (!result.model.has_value() || !result.model->polarity.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            CircuitMosfetModel mosfet_bound{};
            if (!BindAuditedMosfetModel(*result.model, mosfet_bound)) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            component.mosfet_model = mosfet_bound;
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
        if (component.type == CircuitComponentType::BeamTetrode) {
            const auto result = model_database.Bind({*component.model, ModelKind::BeamTetrode});
            if (!result.model.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
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
            component.beam_tetrode_model = CircuitBeamTetrodeModel{
                .kind = "beam-capture", .model_version = 1U,
                .screen_amplification_factor = *amplification, .perveance = *perveance,
                .knee_voltage = *knee, .screen_current_fraction = *fraction,
                .max_current = *max_current, .max_step = *max_step};
            continue;
        }
        if (component.type == CircuitComponentType::VariMu) {
            const auto result = model_database.Bind({*component.model, ModelKind::VariMu});
            if (!result.model.has_value()) {
                return {.circuit = std::nullopt, .error = MnaCompileError::MissingModelAuthorization};
            }
            const auto& record = *result.model;
            const auto amplification = ModelParameter(record, "amplification-factor");
            const auto perveance = ModelParameter(record, "perveance");
            const auto coefficient = ModelParameter(record, "remote-cutoff-coefficient");
            const auto max_current = ModelParameter(record, "max-current");
            const auto max_step = ModelParameter(record, "max-step");
            if (!amplification || !perveance || !coefficient || !max_current || !max_step) {
                return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
            }
            component.vari_mu_model = CircuitVariMuModel{
                .kind = "remote-cutoff", .model_version = 1U,
                .amplification_factor = *amplification, .perveance = *perveance,
                .remote_cutoff_coefficient = *coefficient,
                .max_current = *max_current, .max_step = *max_step};
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
        component.diode_model.emplace();
        if (!BindAuditedDiodeModel(*record, *component.diode_model)) {
            return {.circuit = std::nullopt, .error = MnaCompileError::InvalidModel};
        }
    }
    return CompileMnaCircuit(bound);
}

} // namespace solo_rig
