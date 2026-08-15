#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace solo_rig {

enum class CircuitPortDirection {
    Input,
    Output,
};

enum class CircuitComponentType {
    Resistor,
    Capacitor,
    Inductor,
    VoltageSource,
    Diode,
    Potentiometer,
    Vccs,
    IdealOpAmp,
    OpAmp,
    Ota,
    Bjt,
    Jfet,
    Mosfet,
    Triode,
    Pentode,
    BeamTetrode,
    VariMu,
    Switch,
    CoupledInductor,
    SpeakerLoad,
    Relay,
    Optocoupler,
};

struct CircuitPotentiometerSpec {
    double position{0.5};
    double minimum{0.0};
    double maximum{1.0};
    std::optional<std::string> wiper_node;
};

struct CircuitControlledSourceSpec {
    std::string kind;
    std::uint32_t model_version{1};
    double gain{};
    std::optional<double> minimum;
    std::optional<double> maximum;
};

struct CircuitBjtModel {
    std::string kind{"ebers-moll"};
    std::uint32_t model_version{1};
    std::string polarity{"npn"};
    double saturation_current{1.0e-15};
    double forward_beta{100.0};
    double reverse_beta{1.0};
    double thermal_voltage{0.02585};
    double max_current{1.0e-2};
    double max_step{1.0};
    double early_voltage{0.0};
    double be_junction_capacitance{0.0};
    double bc_junction_capacitance{0.0};
    double be_junction_potential{0.7};
    double bc_junction_potential{0.7};
    double be_grading_coefficient{0.5};
    double bc_grading_coefficient{0.5};
    double temperature_celsius{25.0};
    double leakage_temperature_coefficient{0.0};
};

struct CircuitJfetModel {
    std::string kind{"square-law"};
    std::uint32_t model_version{1};
    std::string polarity{"n-channel"};
    double pinch_off_voltage{1.0};
    double transconductance_parameter{1.0e-3};
    double max_current{1.0e-2};
    double max_step{1.0};
    double channel_length_modulation{0.0};
    double temperature_celsius{25.0};
    double gate_saturation_current{0.0};
    double gate_thermal_voltage{0.02585};
    double gate_junction_capacitance{0.0};
    double gate_junction_potential{0.7};
    double gate_grading_coefficient{0.5};
    double gate_leakage_temperature_coefficient{0.0};
};

struct CircuitMosfetModel {
    std::string kind{"enhancement-square-law"};
    std::uint32_t model_version{1};
    std::string polarity{"n-channel"};
    double threshold_voltage{1.0};
    double transconductance_parameter{1.0e-3};
    double max_current{1.0e-2};
    double max_step{1.0};
    double channel_length_modulation{};
    double subthreshold_current{};
    double subthreshold_slope_voltage{0.08};
    double temperature_celsius{25.0};
    double body_diode_saturation_current{};
    double body_diode_thermal_voltage{0.02585};
    double gate_junction_capacitance{};
    double gate_junction_potential{0.7};
    double gate_grading_coefficient{0.5};
};

struct CircuitAmplifierModel {
    std::string kind;
    std::uint32_t model_version{1};
    double gain{};
    double minimum_output{};
    double maximum_output{};
    double max_step{1.0};
    double gain_bandwidth_hz{};
    double slew_rate{};
    double input_resistance{};
    double input_bias_current{};
    double input_offset{};
    double output_resistance{};
    double output_current_limit{};
    double supply_minimum{};
    double supply_maximum{};
};

struct CircuitTriodeModel {
    std::string kind{"space-charge"};
    std::uint32_t model_version{1};
    double amplification_factor{100.0};
    double perveance{1.0e-3};
    double max_current{1.0e-2};
    double max_step{1.0};
};

struct CircuitPentodeModel {
    std::string kind{"space-charge"};
    std::uint32_t model_version{1};
    double screen_amplification_factor{20.0};
    double perveance{1.0e-3};
    double knee_voltage{2.0};
    double screen_current_fraction{0.1};
    double max_current{1.0e-2};
    double max_step{1.0};
};

struct CircuitBeamTetrodeModel {
    std::string kind{"beam-capture"};
    std::uint32_t model_version{1};
    double screen_amplification_factor{20.0};
    double perveance{1.0e-3};
    double knee_voltage{2.0};
    double screen_current_fraction{0.1};
    double max_current{1.0e-2};
    double max_step{1.0};
};

struct CircuitVariMuModel {
    std::string kind{"remote-cutoff"};
    std::uint32_t model_version{1};
    double amplification_factor{20.0};
    double perveance{1.0e-3};
    double remote_cutoff_coefficient{2.0};
    double max_current{1.0e-2};
    double max_step{1.0};
};

struct CircuitCoupledInductorModel {
    std::string kind{"lossy-linear"};
    std::uint32_t model_version{1};
    double turns_ratio{1.0};
    double magnetizing_inductance{1.0};
    double primary_leakage_inductance{};
    double secondary_leakage_inductance{};
    double primary_resistance{};
    double secondary_resistance{};
    double saturation_flux{1.0};
    double hysteresis_width{0.1};
    double hysteresis_rate{1000.0};
    double remanent_flux{};
    double max_flux{2.0};
    double max_current{10.0};
    double max_step{1.0};
};

struct CircuitSwitchSpec {
    std::vector<std::string> states;
    std::uint32_t initial_state{};
    double open_resistance{1.0e9};
    double closed_resistance{1.0e-3};
};

enum class CircuitParameterBindingProperty {
    PotentiometerPosition,
    SwitchState,
    SupplyVoltage,
};

struct CircuitParameterBinding {
    std::uint32_t model_version{1};
    std::string parameter_id;
    std::string target_id;
    CircuitParameterBindingProperty property{CircuitParameterBindingProperty::PotentiometerPosition};
    double smoothing_seconds{};
};

struct CircuitDiodeModel {
    std::string kind{"shockley"};
    std::uint32_t model_version{1};
    double saturation_current{1.0e-9};
    double thermal_voltage{0.02585};
    double breakdown_voltage{};
    double breakdown_current{};
    double breakdown_slope{};
    double max_step{1.0};
    double junction_capacitance{};
    double junction_potential{0.7};
    double grading_coefficient{0.5};
    double temperature_celsius{25.0};
    double leakage_current{};
    double leakage_temperature_coefficient{};
};

struct CircuitPort {
    std::string id;
    CircuitPortDirection direction{CircuitPortDirection::Input};
    std::string node;
};

struct CircuitSupply {
    std::string id;
    std::string positive_node;
    std::string negative_node;
    double voltage{};
    std::string kind{"ideal"};
    std::uint32_t model_version{1};
    double source_resistance{};
    double sag_resistance{};
    double recovery_time_seconds{};
    double ripple_amplitude{};
    double ripple_frequency_hz{};
    double maximum_sag{};
    double maximum_load_current{};
    double initial_sag{};
};

struct CircuitSpeakerLoadModel {
    std::string kind{"electrodynamic"};
    std::uint32_t model_version{1};
    double voice_coil_resistance{};
    double voice_coil_inductance{};
    double force_factor{};
    double moving_mass{};
    double mechanical_resistance{};
    double compliance{};
    double nonlinear_stiffness{};
    double nonlinear_displacement_scale{};
    double maximum_current{};
    double maximum_velocity{};
    double maximum_displacement{};
    double initial_current{};
    double initial_velocity{};
    double initial_displacement{};
};

struct CircuitRelayModel {
    std::string kind{"generic-dynamic"};
    std::uint32_t model_version{1};
    double coil_resistance{};
    double coil_inductance{};
    double open_resistance{};
    double closed_resistance{};
    double pull_in_current{};
    double drop_out_current{};
    double operate_time_seconds{};
    double release_time_seconds{};
    double bounce_duration_seconds{};
    double bounce_frequency_hz{};
    double bounce_amplitude{};
    double maximum_coil_current{};
    bool initial_energized{};
    std::uint32_t pole_count{1U};
    double contact_gap{};
};

struct CircuitOptocouplerModel {
    std::string kind{"generic-electrical"};
    std::uint32_t model_version{1};
    double dark_resistance_ohms{1.0e6};
    double lit_resistance_ohms{100.0};
    double emitter_threshold_volts{1.0};
    double emitter_scale_volts{4.0};
    double response_exponent{1.0};
    double attack_seconds{0.01};
    double release_seconds{0.1};
    double maximum_drive_volts{5.0};
    double initial_illumination{};
    double emitter_resistance_ohms{220.0};
};

struct CircuitComponent {
    std::string id;
    CircuitComponentType type{CircuitComponentType::Resistor};
    std::vector<std::string> pins;
    std::optional<double> value;
    std::optional<double> tolerance;
    std::optional<double> initial;
    std::optional<std::string> model;
    std::optional<CircuitDiodeModel> diode_model;
    std::optional<CircuitPotentiometerSpec> potentiometer;
    std::optional<CircuitControlledSourceSpec> controlled_source;
    std::optional<CircuitBjtModel> bjt_model;
    std::optional<CircuitJfetModel> jfet_model;
    std::optional<CircuitMosfetModel> mosfet_model;
    std::optional<CircuitAmplifierModel> amplifier_model;
    std::optional<CircuitTriodeModel> triode_model;
    std::optional<CircuitPentodeModel> pentode_model;
    std::optional<CircuitBeamTetrodeModel> beam_tetrode_model;
    std::optional<CircuitVariMuModel> vari_mu_model;
    std::optional<CircuitCoupledInductorModel> coupled_inductor_model;
    std::optional<CircuitSpeakerLoadModel> speaker_load_model;
    std::optional<CircuitRelayModel> relay_model;
    std::optional<CircuitOptocouplerModel> optocoupler_model;
    std::optional<double> primary_initial_current;
    std::optional<double> secondary_initial_current;
    std::optional<CircuitSwitchSpec> switch_spec;
};

struct CircuitConvergenceConfig {
    std::uint32_t max_iterations{8};
    double absolute_tolerance{1.0e-9};
    double relative_tolerance{1.0e-6};
};

struct CircuitSolverConfig {
    std::string method{"modified-nodal-analysis"};
    std::string integration{"backward-euler"};
    std::uint32_t oversampling{1};
    CircuitConvergenceConfig convergence;
};

struct CircuitMeasurementBaseline {
    std::optional<std::string> reference;
    std::optional<double> frequency_hz;
    std::optional<double> tolerance;
};

struct CircuitNetlist {
    std::uint32_t version{2};
    std::string schema{"circuit"};
    std::string reference_ground;
    std::vector<std::string> nodes;
    std::vector<CircuitPort> ports;
    std::vector<CircuitSupply> supplies;
    std::vector<CircuitComponent> components;
    std::vector<CircuitParameterBinding> parameter_bindings;
    CircuitSolverConfig solver;
    std::optional<CircuitMeasurementBaseline> measurement_baseline;
};

enum class CircuitLoadError {
    InvalidJson,
    UnsupportedVersion,
    InvalidSchema,
    MissingField,
    InvalidField,
    DuplicateId,
    InvalidNode,
    InvalidPin,
    InvalidValue,
    MissingModel,
    UnsafeAssetPath,
    UnknownComponent,
    InvalidSolver,
    InvalidConvergence,
    InvalidOversampling,
    EmptyNetlist,
};

struct CircuitLoadResult {
    std::optional<CircuitNetlist> circuit;
    std::vector<CircuitLoadError> errors;
};

[[nodiscard]] CircuitLoadResult ParseCircuitV2Json(std::string_view source);

} // namespace solo_rig
