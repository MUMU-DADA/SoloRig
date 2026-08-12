#include "core/effect_descriptor.hpp"

#include <unordered_set>

namespace solo_rig {

std::vector<DescriptorValidationError> EffectDescriptor::Validate() const {
    std::vector<DescriptorValidationError> errors;

    if (module_id.empty()) {
        errors.push_back(DescriptorValidationError::MissingModuleId);
    }
    if (display_name.empty()) {
        errors.push_back(DescriptorValidationError::MissingDisplayName);
    }
    if (api_version != 1) {
        errors.push_back(DescriptorValidationError::UnsupportedApiVersion);
    }
    if (logic_entry_point.empty()) {
        errors.push_back(DescriptorValidationError::MissingLogicEntryPoint);
    }

    bool has_input = false;
    bool has_output = false;
    std::unordered_set<std::string> port_ids;
    for (const auto& port : ports) {
        has_input = has_input || port.direction == PortDirection::Input;
        has_output = has_output || port.direction == PortDirection::Output;

        if (port.id.empty()) {
            errors.push_back(DescriptorValidationError::MissingPortId);
        } else if (!port_ids.insert(port.id).second) {
            errors.push_back(DescriptorValidationError::DuplicatePortId);
        }

        if (port.max_channels == 0) {
            errors.push_back(DescriptorValidationError::InvalidPortChannelCount);
        }
    }

    if (!has_input) {
        errors.push_back(DescriptorValidationError::MissingInputPort);
    }
    if (!has_output) {
        errors.push_back(DescriptorValidationError::MissingOutputPort);
    }

    std::unordered_set<std::string> parameter_ids;
    for (const auto& parameter : parameters) {
        if (parameter.id.empty()) {
            errors.push_back(DescriptorValidationError::MissingParameterId);
        } else if (!parameter_ids.insert(parameter.id).second) {
            errors.push_back(DescriptorValidationError::DuplicateParameterId);
        }

        if (parameter.minimum > parameter.maximum) {
            errors.push_back(DescriptorValidationError::InvalidParameterRange);
        } else if (parameter.default_value < parameter.minimum || parameter.default_value > parameter.maximum) {
            errors.push_back(DescriptorValidationError::InvalidParameterDefault);
        }
    }

    return errors;
}

} // namespace solo_rig
