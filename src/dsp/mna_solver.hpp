#pragma once

#include "content/circuit_v2.hpp"
#include "content/model_database.hpp"
#include "core/realtime_contract.hpp"
#include "dsp/coupled_inductor.hpp"
#include "dsp/dynamic_power_supply.hpp"
#include "dsp/dynamic_speaker_load.hpp"
#include "dsp/dynamic_relay.hpp"
#include "dsp/dynamic_relay_routing.hpp"
#include "dsp/nonlinear_device.hpp"
#include "dsp/nonlinear_diode_dynamic.hpp"
#include "dsp/nonlinear_bjt_dynamic.hpp"
#include "dsp/nonlinear_jfet_dynamic.hpp"
#include "dsp/nonlinear_transformer_core.hpp"
#include "dsp/nonlinear_opamp_dynamic.hpp"

#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <string_view>
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
    [[nodiscard]] std::size_t ControlCount() const noexcept;
    [[nodiscard]] std::optional<std::size_t> FindControl(std::string_view parameter_id) const noexcept;
    [[nodiscard]] bool SetControlTarget(std::size_t control_index, double value) noexcept;
    [[nodiscard]] std::optional<double> ControlValue(std::size_t control_index) const noexcept;
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
        std::int32_t triode_grid_node{-1};
        std::int32_t pentode_screen_node{-1};
        std::int32_t pentode_grid_node{-1};
        std::int32_t beam_tetrode_screen_node{-1};
        std::int32_t beam_tetrode_grid_node{-1};
        std::int32_t vari_mu_grid_node{-1};
        std::uint32_t branch_index{};
        std::uint32_t history_index{UINT32_MAX};
        std::uint32_t inductor_history_index{UINT32_MAX};
        double value{};
        double initial{};
        NonlinearDeviceModel nonlinear_model{};
        NonlinearDiodeDynamicModel nonlinear_diode_dynamic_model{};
        double controlled_gain{};
        double controlled_minimum{};
        double controlled_maximum{};
        bool controlled_has_limits{};
        NonlinearBjtModel nonlinear_bjt_model{};
        NonlinearBjtDynamicModel nonlinear_bjt_dynamic_model{};
        std::uint32_t bjt_be_history_index{UINT32_MAX};
        std::uint32_t bjt_bc_history_index{UINT32_MAX};
        NonlinearJfetModel nonlinear_jfet_model{};
        NonlinearJfetDynamicModel nonlinear_jfet_dynamic_model{};
        std::uint32_t jfet_gs_history_index{UINT32_MAX};
        std::uint32_t jfet_gd_history_index{UINT32_MAX};
        NonlinearMosfetModel nonlinear_mosfet_model{};
        NonlinearMosfetDynamicModel nonlinear_mosfet_dynamic_model{};
        std::uint32_t mosfet_gs_history_index{UINT32_MAX};
        std::uint32_t mosfet_gd_history_index{UINT32_MAX};
        NonlinearAmplifierModel nonlinear_amplifier_model{};
        NonlinearOpAmpDynamicModel nonlinear_opamp_dynamic_model{};
        std::uint32_t opamp_dynamic_history_index{UINT32_MAX};
        NonlinearTriodeModel nonlinear_triode_model{};
        NonlinearPentodeModel nonlinear_pentode_model{};
        NonlinearBeamTetrodeModel nonlinear_beam_tetrode_model{};
        NonlinearVariMuModel nonlinear_vari_mu_model{};
        std::int32_t secondary_positive_node{-1};
        std::int32_t secondary_negative_node{-1};
        std::uint32_t secondary_branch_index{UINT32_MAX};
        std::uint32_t secondary_inductor_history_index{UINT32_MAX};
        std::uint32_t transformer_core_history_index{UINT32_MAX};
        double secondary_initial{};
        CoupledInductorModel coupled_inductor_model{};
        CoupledInductorCoefficients coupled_inductor_coefficients{};
        NonlinearTransformerCoreModel nonlinear_transformer_core_model{};
        DynamicPowerSupplyModel dynamic_power_supply_model{};
        std::uint32_t dynamic_power_supply_state_index{UINT32_MAX};
        DynamicSpeakerLoadModel dynamic_speaker_load_model{};
        DynamicSpeakerLoadState dynamic_speaker_load_initial_state{};
        std::uint32_t dynamic_speaker_load_state_index{UINT32_MAX};
        DynamicRelayModel dynamic_relay_model{};
        DynamicRelayRoutingModel dynamic_relay_routing_model{};
        DynamicRelayState dynamic_relay_initial_state{};
        std::uint32_t dynamic_relay_state_index{UINT32_MAX};
        std::uint32_t dynamic_relay_model_version{1U};
        std::array<std::int32_t, 4> relay_common_nodes{-1, -1, -1, -1};
        std::array<std::int32_t, 4> relay_normally_closed_nodes{-1, -1, -1, -1};
        std::array<std::int32_t, 4> relay_normally_open_nodes{-1, -1, -1, -1};
    };

private:
    friend struct MnaCompileResult;

    enum class ControlKind {
        PotentiometerPosition,
        SwitchState,
        SupplyVoltage,
    };

    struct Control {
        ControlKind kind{ControlKind::PotentiometerPosition};
        std::string parameter_id;
        std::uint32_t primary_element_index{UINT32_MAX};
        std::uint32_t secondary_element_index{UINT32_MAX};
        double minimum{};
        double maximum{};
        double initial{};
        double current{};
        double target{};
        double smoothing_seconds{};
        double scale_a{};
        double scale_b{};
    };

    [[nodiscard]] bool ApplyControl(Control& control) noexcept;
    [[nodiscard]] bool AdvanceControls(double timestep_seconds) noexcept;

    std::vector<Element> elements_;
    std::vector<double> capacitor_history_;
    std::vector<double> inductor_history_;
    std::vector<NonlinearTransformerCoreState> transformer_core_history_;
    std::vector<double> bjt_be_history_;
    std::vector<double> bjt_bc_history_;
    std::vector<double> jfet_gs_history_;
    std::vector<double> jfet_gd_history_;
    std::vector<double> mosfet_gs_history_;
    std::vector<double> mosfet_gd_history_;
    std::vector<double> opamp_dynamic_history_;
    std::vector<DynamicPowerSupplyState> dynamic_power_supply_states_;
    std::vector<DynamicSpeakerLoadState> dynamic_speaker_load_states_;
    std::vector<DynamicRelayState> dynamic_relay_states_;
    std::vector<Control> controls_;
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
