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
    Bjt,
    Jfet,
    Mosfet,
    Switch,
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
