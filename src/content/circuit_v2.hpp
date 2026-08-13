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
    VoltageSource,
    Diode,
    Potentiometer,
    Switch,
};

struct CircuitPotentiometerSpec {
    double position{0.5};
    double minimum{0.0};
    double maximum{1.0};
};

struct CircuitSwitchSpec {
    std::vector<std::string> states;
    std::uint32_t initial_state{};
};

struct CircuitDiodeModel {
    std::string kind{"shockley"};
    double saturation_current{1.0e-9};
    double thermal_voltage{0.02585};
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
