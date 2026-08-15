#pragma once

#include "content/module_package.hpp"
#include "content/model_database.hpp"
#include "core/audio_graph.hpp"

#include <memory>
#include <optional>
#include <string_view>
#include <variant>

namespace solo_rig {

enum class ModuleRuntimeError {
    MissingLogic,
    LogicCategoryMismatch,
    UnsupportedCategory,
    UnsupportedAnalogTopology,
    UnsupportedDigitalOperation,
    InvalidDigitalTopology,
    InvalidInitialParameterValue,
    UnsupportedPortLayout,
    UnsupportedCircuit,
    ExternalAssetLoadFailed,
};

// A concrete instance of an external module package. Parameter changes must
// happen on the control thread; Process itself performs no allocation or file
// access after Prepare has succeeded.
class ModuleRuntimeProcessor : public EffectProcessor {
public:
    ~ModuleRuntimeProcessor() override = default;

    [[nodiscard]] virtual bool SetParameter(std::string_view parameter_id, float value) noexcept = 0;
    [[nodiscard]] virtual std::optional<float> ParameterValue(std::string_view parameter_id) const noexcept = 0;
};

[[nodiscard]] std::variant<std::unique_ptr<ModuleRuntimeProcessor>, ModuleRuntimeError> CreateModuleRuntime(
    const ModulePackage& package);
[[nodiscard]] std::variant<std::unique_ptr<ModuleRuntimeProcessor>, ModuleRuntimeError> CreateModuleRuntime(
    const ModulePackage& package, const ModelDatabase& model_database);

} // namespace solo_rig
