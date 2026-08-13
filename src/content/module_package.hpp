#pragma once

#include "core/effect_descriptor.hpp"
#include "content/circuit_v2.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <variant>

namespace solo_rig {

enum class AnalogCircuitSolver {
    ModifiedNodalAnalysis,
};

enum class AnalogComponentType {
    Resistor,
    Capacitor,
    Inductor,
    Diode,
    Potentiometer,
    OperationalAmplifier,
    Transistor,
    VoltageSource,
};

struct AnalogCircuitComponent {
    std::string id;
    AnalogComponentType type{AnalogComponentType::Resistor};
    std::vector<std::string> nodes;
    std::optional<double> value;
    std::optional<std::string> model;
};

struct AnalogCircuitLogic {
    AnalogCircuitSolver solver{AnalogCircuitSolver::ModifiedNodalAnalysis};
    std::vector<AnalogCircuitComponent> components;
};

enum class DigitalOperationType {
    Gain,
    Delay,
    Feedback,
    Mix,
    Filter,
    Waveshaper,
    Dynamics,
    PitchShift,
    Bypass,
};

struct DigitalAlgorithmOperation {
    DigitalOperationType type{DigitalOperationType::Gain};
    std::optional<std::string> parameter_id;
};

struct DigitalAlgorithmLogic {
    std::string algorithm_id;
    std::vector<DigitalAlgorithmOperation> operations;
};

using ModuleLogic = std::variant<std::monostate, AnalogCircuitLogic, CircuitNetlist, DigitalAlgorithmLogic>;

enum class ModulePackageLoadError {
    MissingManifest,
    CannotReadManifest,
    InvalidJson,
    InvalidSchema,
    MissingRequiredField,
    InvalidField,
    UnsafeAssetPath,
    DescriptorInvalid,
    MissingLogicFile,
    CannotReadLogicFile,
    InvalidLogicJson,
    InvalidLogicSchema,
    LogicCategoryMismatch,
    InvalidLogicDefinition,
    LogicParameterUnknown,
};

struct ModulePackage {
    EffectDescriptor descriptor;
    std::filesystem::path root_directory;
    std::filesystem::path logic_path;
    std::optional<std::filesystem::path> custom_skin_path;
    ModuleLogic logic;
};

struct ModulePackageLoadResult {
    std::optional<ModulePackage> package;
    std::vector<ModulePackageLoadError> errors;
};

[[nodiscard]] bool IsSafeRelativeAssetPath(std::string_view path);
[[nodiscard]] ModulePackageLoadResult LoadModulePackage(const std::filesystem::path& root_directory);

} // namespace solo_rig
