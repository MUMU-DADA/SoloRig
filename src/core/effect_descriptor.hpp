#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace solo_rig {

enum class EffectCategory {
    AnalogCircuit,
    DigitalAlgorithm,
    NeuralAmpModel,
    ImpulseResponse,
    Utility,
};

enum class PortDirection {
    Input,
    Output,
};

struct EffectPort {
    std::string id;
    PortDirection direction{PortDirection::Input};
    std::uint32_t max_channels{};
};

struct EffectParameter {
    std::string id;
    float minimum{};
    float maximum{};
    float default_value{};
};

enum class DescriptorValidationError {
    MissingModuleId,
    MissingDisplayName,
    UnsupportedApiVersion,
    MissingLogicEntryPoint,
    MissingInputPort,
    MissingOutputPort,
    MissingPortId,
    DuplicatePortId,
    InvalidPortChannelCount,
    MissingParameterId,
    DuplicateParameterId,
    InvalidParameterRange,
    InvalidParameterDefault,
};

struct EffectDescriptor {
    std::string module_id;
    std::string display_name;
    std::uint32_t api_version{};
    EffectCategory category{EffectCategory::Utility};
    std::string logic_entry_point;
    std::vector<EffectPort> ports;
    std::vector<EffectParameter> parameters;
    std::uint32_t declared_latency_samples{};

    [[nodiscard]] std::vector<DescriptorValidationError> Validate() const;
};

} // namespace solo_rig
