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
};

struct CircuitJfetModel {
    std::string kind{"square-law"};
    std::uint32_t model_version{1};
    std::string polarity{"n-channel"};
    double pinch_off_voltage{1.0};
    double transconductance_parameter{1.0e-3};
    double max_current{1.0e-2};
    double max_step{1.0};
};

struct CircuitMosfetModel {
    std::string kind{"enhancement-square-law"};
    std::uint32_t model_version{1};
    std::string polarity{"n-channel"};
    double threshold_voltage{1.0};
    double transconductance_parameter{1.0e-3};
    double max_current{1.0e-2};
    double max_step{1.0};
};

struct CircuitAmplifierModel {
    std::string kind;
    std::uint32_t model_version{1};
    double gain{};
    double minimum_output{};
    double maximum_output{};
    double max_step{1.0};
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
};

struct CircuitSwitchSpec {
    std::vector<std::string> states;
    std::uint32_t initial_state{};
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
