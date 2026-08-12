#include "core/effect_descriptor.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <ranges>

using solo_rig::EffectCategory;
using solo_rig::EffectDescriptor;
using solo_rig::EffectParameter;
using solo_rig::EffectPort;
using solo_rig::PortDirection;

namespace {

EffectDescriptor CreateValidDescriptor() {
    return {
        .module_id = "org.mumu-dada.solorig.overdrive",
        .display_name = "Reference Overdrive",
        .api_version = 1,
        .category = EffectCategory::AnalogCircuit,
        .logic_entry_point = "logic/circuit.json",
        .ports = {
            {.id = "input", .direction = PortDirection::Input, .max_channels = 2},
            {.id = "output", .direction = PortDirection::Output, .max_channels = 2},
        },
        .parameters = {
            {.id = "drive", .minimum = 0.0F, .maximum = 1.0F, .default_value = 0.5F},
        },
        .declared_latency_samples = 0,
    };
}

bool Contains(const std::vector<solo_rig::DescriptorValidationError>& errors,
              solo_rig::DescriptorValidationError expected) {
    return std::ranges::find(errors, expected) != errors.end();
}

} // namespace

TEST_CASE("EffectDescriptor accepts a complete module contract") {
    REQUIRE(CreateValidDescriptor().Validate().empty());
}

TEST_CASE("EffectDescriptor requires stable module and logic identifiers") {
    auto descriptor = CreateValidDescriptor();
    descriptor.module_id.clear();
    descriptor.logic_entry_point.clear();

    const auto errors = descriptor.Validate();
    REQUIRE(Contains(errors, solo_rig::DescriptorValidationError::MissingModuleId));
    REQUIRE(Contains(errors, solo_rig::DescriptorValidationError::MissingLogicEntryPoint));
}

TEST_CASE("EffectDescriptor rejects duplicate parameter identifiers and invalid ranges") {
    auto descriptor = CreateValidDescriptor();
    descriptor.parameters.push_back({.id = "drive", .minimum = 0.0F, .maximum = 1.0F, .default_value = 0.25F});
    descriptor.parameters.push_back({.id = "level", .minimum = 1.0F, .maximum = 0.0F, .default_value = 0.5F});

    const auto errors = descriptor.Validate();
    REQUIRE(Contains(errors, solo_rig::DescriptorValidationError::DuplicateParameterId));
    REQUIRE(Contains(errors, solo_rig::DescriptorValidationError::InvalidParameterRange));
}

TEST_CASE("EffectDescriptor requires one input and one output port") {
    auto descriptor = CreateValidDescriptor();
    descriptor.ports.pop_back();

    const auto errors = descriptor.Validate();
    REQUIRE(Contains(errors, solo_rig::DescriptorValidationError::MissingOutputPort));
}
