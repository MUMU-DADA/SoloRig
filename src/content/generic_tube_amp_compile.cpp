#include "content/generic_tube_amp_compile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace solo_rig {
namespace {

struct PortDefinition {
    std::string_view id;
    std::string_view pin;
    GenericTubeAmpPortDirection direction;
    GenericTubeAmpPortDomain domain;
};

struct ComponentDefinition {
    std::string_view id;
    GenericTubeAmpComponentKind kind;
    std::string_view model_reference;
    std::size_t pin_count;
};

struct TerminalDefinition {
    GenericTubeAmpTerminalOwner owner;
    std::string_view owner_id;
    std::string_view terminal_id;
};

struct NetDefinition {
    std::string_view id;
    GenericTubeAmpPortDomain domain;
    std::array<TerminalDefinition, kGenericTubeAmpMaximumNetTerminals> terminals{};
    std::size_t terminal_count{};
};

constexpr std::size_t kRequiredStageCount = kGenericTubeAmpMaximumStages;
constexpr std::size_t kRequiredComponentCount = 11U;
constexpr std::size_t kRequiredNetCount = kGenericTubeAmpMaximumNets;

std::size_t PortCount(GenericTubeAmpStageKind kind) noexcept {
    switch (kind) {
    case GenericTubeAmpStageKind::Input:
    case GenericTubeAmpStageKind::Output:
        return 1U;
    case GenericTubeAmpStageKind::Triode:
        return 3U;
    case GenericTubeAmpStageKind::CouplingNetwork:
        return 2U;
    case GenericTubeAmpStageKind::Pentode:
    case GenericTubeAmpStageKind::BeamTetrode:
        return 4U;
    case GenericTubeAmpStageKind::Supply:
        return 2U;
    case GenericTubeAmpStageKind::Transformer:
        return 4U;
    case GenericTubeAmpStageKind::Speaker:
        return 2U;
    }
    return 0U;
}

PortDefinition Definition(GenericTubeAmpStageKind kind, std::size_t index) noexcept {
    switch (kind) {
    case GenericTubeAmpStageKind::Input:
        return index == 0U ? PortDefinition{"audio", "input", GenericTubeAmpPortDirection::Output,
                                            GenericTubeAmpPortDomain::Audio}
                          : PortDefinition{};
    case GenericTubeAmpStageKind::Triode:
        if (index == 0U) return {"plate", "plate", GenericTubeAmpPortDirection::Terminal,
                                 GenericTubeAmpPortDomain::Audio};
        if (index == 1U) return {"grid", "grid", GenericTubeAmpPortDirection::Terminal,
                                 GenericTubeAmpPortDomain::Audio};
        if (index == 2U) return {"cathode", "cathode", GenericTubeAmpPortDirection::Terminal,
                                 GenericTubeAmpPortDomain::Bias};
        return {};
    case GenericTubeAmpStageKind::CouplingNetwork:
        if (index == 0U) return {"in", "in", GenericTubeAmpPortDirection::Terminal,
                                 GenericTubeAmpPortDomain::Audio};
        if (index == 1U) return {"out", "out", GenericTubeAmpPortDirection::Terminal,
                                 GenericTubeAmpPortDomain::Audio};
        return {};
    case GenericTubeAmpStageKind::Pentode:
    case GenericTubeAmpStageKind::BeamTetrode:
        if (index == 0U) return {"plate", "plate", GenericTubeAmpPortDirection::Terminal,
                                 GenericTubeAmpPortDomain::TransformerPrimary};
        if (index == 1U) return {"screen", "screen", GenericTubeAmpPortDirection::Terminal,
                                 GenericTubeAmpPortDomain::SupplyRail};
        if (index == 2U) return {"grid", "grid", GenericTubeAmpPortDirection::Terminal,
                                 GenericTubeAmpPortDomain::Audio};
        if (index == 3U) return {"cathode", "cathode", GenericTubeAmpPortDirection::Terminal,
                                 GenericTubeAmpPortDomain::Bias};
        return {};
    case GenericTubeAmpStageKind::Supply:
        if (index == 0U) return {"positive", "positive", GenericTubeAmpPortDirection::Terminal,
                                 GenericTubeAmpPortDomain::SupplyRail};
        if (index == 1U) return {"negative", "negative", GenericTubeAmpPortDirection::Terminal,
                                 GenericTubeAmpPortDomain::Ground};
        return {};
    case GenericTubeAmpStageKind::Transformer:
        if (index == 0U) return {"primary_dot", "primary_dot", GenericTubeAmpPortDirection::Terminal,
                                 GenericTubeAmpPortDomain::TransformerPrimary};
        if (index == 1U) return {"primary_return", "primary_return", GenericTubeAmpPortDirection::Terminal,
                                 GenericTubeAmpPortDomain::SupplyRail};
        if (index == 2U) return {"secondary_dot", "secondary_dot", GenericTubeAmpPortDirection::Terminal,
                                 GenericTubeAmpPortDomain::TransformerSecondary};
        if (index == 3U) return {"secondary_return", "secondary_return", GenericTubeAmpPortDirection::Terminal,
                                 GenericTubeAmpPortDomain::TransformerSecondary};
        return {};
    case GenericTubeAmpStageKind::Speaker:
        if (index == 0U) return {"positive", "positive", GenericTubeAmpPortDirection::Terminal,
                                 GenericTubeAmpPortDomain::TransformerSecondary};
        if (index == 1U) return {"negative", "negative", GenericTubeAmpPortDirection::Terminal,
                                 GenericTubeAmpPortDomain::TransformerSecondary};
        return {};
    case GenericTubeAmpStageKind::Output:
        return index == 0U ? PortDefinition{"audio", "output", GenericTubeAmpPortDirection::Input,
                                            GenericTubeAmpPortDomain::TransformerSecondary}
                          : PortDefinition{};
    }
    return {};
}

std::string_view RequiredModel(GenericTubeAmpStageKind kind) noexcept {
    switch (kind) {
    case GenericTubeAmpStageKind::Input:
    case GenericTubeAmpStageKind::Output:
        return "external-audio-v1";
    case GenericTubeAmpStageKind::Triode:
        return "generic-triode-v1";
    case GenericTubeAmpStageKind::CouplingNetwork:
        return "generic-passive-coupling-v1";
    case GenericTubeAmpStageKind::Pentode:
        return "generic-pentode-v1";
    case GenericTubeAmpStageKind::BeamTetrode:
        return "generic-beam-tetrode-v1";
    case GenericTubeAmpStageKind::Supply:
        return "generic-rectified-reservoir-v1";
    case GenericTubeAmpStageKind::Transformer:
        return "generic-saturating-hysteretic-transformer-v1";
    case GenericTubeAmpStageKind::Speaker:
        return "generic-electrodynamic-speaker-v1";
    }
    return {};
}

ComponentDefinition ComponentAt(std::size_t index) noexcept {
    constexpr std::array<ComponentDefinition, kRequiredComponentCount> definitions{
        ComponentDefinition{"input_coupling", GenericTubeAmpComponentKind::Capacitor,
                             "generic-capacitor-v1", 2U},
        ComponentDefinition{"triode_grid_leak", GenericTubeAmpComponentKind::Resistor,
                             "generic-resistor-v1", 2U},
        ComponentDefinition{"triode_cathode_path", GenericTubeAmpComponentKind::Resistor,
                             "generic-resistor-v1", 2U},
        ComponentDefinition{"triode_plate_load", GenericTubeAmpComponentKind::Resistor,
                             "generic-resistor-v1", 2U},
        ComponentDefinition{"interstage_coupling", GenericTubeAmpComponentKind::Capacitor,
                             "generic-capacitor-v1", 2U},
        ComponentDefinition{"power_grid_bias", GenericTubeAmpComponentKind::Resistor,
                             "generic-resistor-v1", 2U},
        ComponentDefinition{"power_cathode_path", GenericTubeAmpComponentKind::Resistor,
                             "generic-resistor-v1", 2U},
        ComponentDefinition{"screen_feed", GenericTubeAmpComponentKind::Resistor,
                             "generic-resistor-v1", 2U},
        ComponentDefinition{"power_plate_load", GenericTubeAmpComponentKind::Resistor,
                             "generic-resistor-v1", 2U},
        ComponentDefinition{"transformer_core", GenericTubeAmpComponentKind::CoupledInductor,
                             "generic-saturating-hysteretic-transformer-v1", 4U},
        ComponentDefinition{"speaker_load", GenericTubeAmpComponentKind::SpeakerLoad,
                             "generic-electrodynamic-speaker-v1", 2U},
    };
    return index < definitions.size() ? definitions[index] : ComponentDefinition{};
}

std::string_view ComponentPinId(GenericTubeAmpComponentKind kind, std::size_t index) noexcept {
    if (kind == GenericTubeAmpComponentKind::CoupledInductor) {
        constexpr std::array<std::string_view, 4U> ids{
            "primary_dot", "primary_return", "secondary_dot", "secondary_return"};
        return index < ids.size() ? ids[index] : std::string_view{};
    }
    if (kind == GenericTubeAmpComponentKind::Resistor ||
        kind == GenericTubeAmpComponentKind::Capacitor) {
        return index == 0U ? "positive" : index == 1U ? "negative" : std::string_view{};
    }
    if (kind == GenericTubeAmpComponentKind::SpeakerLoad) {
        return index == 0U ? "positive" : index == 1U ? "negative" : std::string_view{};
    }
    return {};
}

NetDefinition NetAt(std::size_t index) noexcept {
    using Owner = GenericTubeAmpTerminalOwner;
    switch (index) {
    case 0U:
        return {"input_audio", GenericTubeAmpPortDomain::Audio,
            {{{Owner::Stage, "input", "audio"}, {Owner::Component, "input_coupling", "positive"}}}, 2U};
    case 1U:
        return {"triode_grid", GenericTubeAmpPortDomain::Audio,
            {{{Owner::Stage, "triode", "grid"}, {Owner::Component, "input_coupling", "negative"},
              {Owner::Component, "triode_grid_leak", "positive"}}}, 3U};
    case 2U:
        return {"triode_plate", GenericTubeAmpPortDomain::Audio,
            {{{Owner::Stage, "triode", "plate"}, {Owner::Stage, "coupling", "in"},
              {Owner::Component, "triode_plate_load", "negative"},
              {Owner::Component, "interstage_coupling", "positive"}}}, 4U};
    case 3U:
        return {"power_grid", GenericTubeAmpPortDomain::Audio,
            {{{Owner::Stage, "power", "grid"}, {Owner::Stage, "coupling", "out"},
              {Owner::Component, "interstage_coupling", "negative"},
              {Owner::Component, "power_grid_bias", "positive"}}}, 4U};
    case 4U:
        return {"triode_cathode", GenericTubeAmpPortDomain::Bias,
            {{{Owner::Stage, "triode", "cathode"},
              {Owner::Component, "triode_cathode_path", "positive"}}}, 2U};
    case 5U:
        return {"power_cathode", GenericTubeAmpPortDomain::Bias,
            {{{Owner::Stage, "power", "cathode"},
              {Owner::Component, "power_cathode_path", "positive"}}}, 2U};
    case 6U:
        return {"supply_rail", GenericTubeAmpPortDomain::SupplyRail,
            {{{Owner::Stage, "supply", "positive"}, {Owner::Component, "triode_plate_load", "positive"},
              {Owner::Component, "screen_feed", "positive"},
              {Owner::Component, "power_plate_load", "positive"},
              {Owner::Stage, "transformer", "primary_return"}}}, 5U};
    case 7U:
        return {"power_plate", GenericTubeAmpPortDomain::TransformerPrimary,
            {{{Owner::Stage, "power", "plate"}, {Owner::Stage, "transformer", "primary_dot"},
              {Owner::Component, "power_plate_load", "negative"}}}, 3U};
    case 8U:
        return {"power_screen", GenericTubeAmpPortDomain::SupplyRail,
            {{{Owner::Stage, "power", "screen"}, {Owner::Component, "screen_feed", "negative"}}}, 2U};
    case 9U:
        return {"secondary_dot", GenericTubeAmpPortDomain::TransformerSecondary,
            {{{Owner::Stage, "transformer", "secondary_dot"}, {Owner::Stage, "speaker", "positive"},
              {Owner::Stage, "output", "audio"}, {Owner::Component, "transformer_core", "secondary_dot"},
              {Owner::Component, "speaker_load", "positive"}}}, 5U};
    case 10U:
        return {"ground", GenericTubeAmpPortDomain::Ground,
            {{{Owner::Stage, "supply", "negative"}, {Owner::Component, "triode_grid_leak", "negative"},
              {Owner::Component, "triode_cathode_path", "negative"},
              {Owner::Component, "power_grid_bias", "negative"},
              {Owner::Component, "power_cathode_path", "negative"},
              {Owner::Stage, "transformer", "secondary_return"},
              {Owner::Stage, "speaker", "negative"},
              {Owner::Component, "transformer_core", "secondary_return"},
              {Owner::Component, "speaker_load", "negative"}}}, 9U};
    }
    return {};
}

template <std::size_t Capacity>
bool CopyText(GenericTubeAmpFixedText<Capacity>& destination, std::string_view source) noexcept {
    if (source.empty() || source.size() >= Capacity || source.size() > UINT8_MAX) return false;
    std::copy(source.begin(), source.end(), destination.value.begin());
    destination.size = static_cast<std::uint8_t>(source.size());
    return true;
}

std::size_t FindStage(const GenericTubeAmpCompileRequest& request, std::string_view id) noexcept {
    for (std::size_t index = 0U; index < request.stage_count; ++index) {
        if (request.stages[index].id == id) return index;
    }
    return kGenericTubeAmpMaximumStages;
}

std::size_t FindComponent(const GenericTubeAmpCompileRequest& request, std::string_view id) noexcept {
    for (std::size_t index = 0U; index < request.component_count; ++index) {
        if (request.components[index].id == id) return index;
    }
    return kGenericTubeAmpMaximumComponents;
}

std::size_t FindNet(const GenericTubeAmpCompileRequest& request, std::string_view id) noexcept {
    for (std::size_t index = 0U; index < request.net_count; ++index) {
        if (request.nets[index].id == id) return index;
    }
    return kGenericTubeAmpMaximumNets;
}

bool SameTerminal(const GenericTubeAmpNetTerminalRequest& actual,
                  const TerminalDefinition& expected) noexcept {
    return actual.owner == expected.owner && actual.owner_id == expected.owner_id &&
        actual.terminal_id == expected.terminal_id;
}

bool HasExpectedTerminal(const GenericTubeAmpNetRequest& net,
                         const TerminalDefinition& expected) noexcept {
    for (std::size_t index = 0U; index < net.terminal_count; ++index) {
        if (SameTerminal(net.terminals[index], expected)) return true;
    }
    return false;
}

bool CompatibleNetDomain(GenericTubeAmpPortDomain net_domain,
                         GenericTubeAmpPortDomain terminal_domain,
                         std::string_view node) noexcept {
    // The reference node is a physical common return.  A transformer
    // secondary return may terminate there while retaining its secondary
    // domain; all other cross-domain joins remain rejected.
    return net_domain == terminal_domain ||
        (node == "ground" && net_domain == GenericTubeAmpPortDomain::Ground &&
         terminal_domain == GenericTubeAmpPortDomain::TransformerSecondary);
}

GenericTubeAmpCompileError CopyStage(GenericTubeAmpStagePlan& destination,
                                     const GenericTubeAmpStageRequest& source) noexcept {
    destination.kind = source.kind;
    destination.port_count = source.port_count;
    destination.parameters = source.parameters;
    if (!CopyText(destination.id, source.id) || !CopyText(destination.model_reference, source.model_reference)) {
        return GenericTubeAmpCompileError::CapacityExceeded;
    }
    for (std::size_t index = 0U; index < source.port_count; ++index) {
        auto& port = destination.ports[index];
        const auto& input = source.ports[index];
        port.direction = input.direction;
        port.domain = input.domain;
        if (!CopyText(port.id, input.id) || !CopyText(port.node, input.node) ||
            !CopyText(port.pin, input.pin)) return GenericTubeAmpCompileError::CapacityExceeded;
    }
    return GenericTubeAmpCompileError::None;
}

GenericTubeAmpCompileError CopyComponent(GenericTubeAmpComponentPlan& destination,
                                          const GenericTubeAmpComponentRequest& source) noexcept {
    destination.kind = source.kind;
    destination.pin_count = source.pin_count;
    destination.parameters = source.parameters;
    if (!CopyText(destination.id, source.id) || !CopyText(destination.model_reference, source.model_reference)) {
        return GenericTubeAmpCompileError::CapacityExceeded;
    }
    for (std::size_t index = 0U; index < source.pin_count; ++index) {
        auto& pin = destination.pins[index];
        const auto& input = source.pins[index];
        pin.domain = input.domain;
        if (!CopyText(pin.id, input.id) || !CopyText(pin.node, input.node)) {
            return GenericTubeAmpCompileError::CapacityExceeded;
        }
    }
    return GenericTubeAmpCompileError::None;
}

GenericTubeAmpCompileError CopySupply(GenericTubeAmpSupplyPlan& destination,
                                      const GenericTubeAmpSupplyRequest& source) noexcept {
    destination.parameters = source.parameters;
    if (!CopyText(destination.id, source.id) || !CopyText(destination.model_reference, source.model_reference) ||
        !CopyText(destination.positive_node, source.positive_node) ||
        !CopyText(destination.negative_node, source.negative_node)) return GenericTubeAmpCompileError::CapacityExceeded;
    return GenericTubeAmpCompileError::None;
}

GenericTubeAmpCompileError CopyNet(GenericTubeAmpNetPlan& destination,
                                   const GenericTubeAmpNetRequest& source) noexcept {
    destination.domain = source.domain;
    destination.terminal_count = source.terminal_count;
    if (!CopyText(destination.id, source.id) || !CopyText(destination.node, source.node)) {
        return GenericTubeAmpCompileError::CapacityExceeded;
    }
    for (std::size_t index = 0U; index < source.terminal_count; ++index) {
        destination.terminals[index].owner = source.terminals[index].owner;
        if (!CopyText(destination.terminals[index].owner_id, source.terminals[index].owner_id) ||
            !CopyText(destination.terminals[index].terminal_id, source.terminals[index].terminal_id)) {
            return GenericTubeAmpCompileError::CapacityExceeded;
        }
    }
    return GenericTubeAmpCompileError::None;
}

} // namespace

GenericTubeAmpCompileResult CompileGenericTubeAmp(
    const GenericTubeAmpCompileRequest& request) noexcept {
    if (request.stage_count > kGenericTubeAmpMaximumStages ||
        request.component_count > kGenericTubeAmpMaximumComponents ||
        request.supply_count > kGenericTubeAmpMaximumSupplies ||
        request.net_count > kGenericTubeAmpMaximumNets) {
        return {.error = GenericTubeAmpCompileError::CapacityExceeded};
    }
    if (request.stage_count != kRequiredStageCount) {
        return {.error = GenericTubeAmpCompileError::MissingStage};
    }
    if (request.component_count != kRequiredComponentCount) {
        return {.error = request.component_count < kRequiredComponentCount
            ? GenericTubeAmpCompileError::MissingComponent : GenericTubeAmpCompileError::UnsupportedTopology};
    }
    if (request.supply_count != 1U) return {.error = GenericTubeAmpCompileError::InvalidSupply};
    if (request.net_count != kRequiredNetCount) {
        return {.error = request.net_count < kRequiredNetCount
            ? GenericTubeAmpCompileError::MissingNet : GenericTubeAmpCompileError::UnsupportedTopology};
    }

    std::array<bool, kGenericTubeAmpMaximumStages + 1U> stage_seen{};
    std::array<std::size_t, kGenericTubeAmpMaximumStages + 1U> stage_indices{};
    stage_indices.fill(kGenericTubeAmpMaximumStages);
    std::size_t power_stage = kGenericTubeAmpMaximumStages;
    std::size_t output_stage = kGenericTubeAmpMaximumStages;
    for (std::size_t index = 0U; index < request.stage_count; ++index) {
        const auto& stage = request.stages[index];
        if (stage.id.empty() || stage.model_reference.empty() || stage.port_count > kGenericTubeAmpMaximumComponentPins) {
            return {.error = stage.port_count > kGenericTubeAmpMaximumComponentPins
                ? GenericTubeAmpCompileError::CapacityExceeded : GenericTubeAmpCompileError::InvalidPlan};
        }
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (request.stages[prior].id == stage.id) return {.error = GenericTubeAmpCompileError::DuplicateStage};
        }
        if (stage.kind == GenericTubeAmpStageKind::Pentode || stage.kind == GenericTubeAmpStageKind::BeamTetrode) {
            if (power_stage != kGenericTubeAmpMaximumStages) return {.error = GenericTubeAmpCompileError::DuplicateStage};
            power_stage = index;
        } else if (stage.kind == GenericTubeAmpStageKind::Output) {
            if (output_stage != kGenericTubeAmpMaximumStages) return {.error = GenericTubeAmpCompileError::DuplicateStage};
            output_stage = index;
        } else {
            const auto kind_index = static_cast<std::size_t>(stage.kind);
            if (kind_index >= stage_seen.size() || stage_seen[kind_index]) {
                return {.error = GenericTubeAmpCompileError::DuplicateStage};
            }
            stage_seen[kind_index] = true;
            stage_indices[kind_index] = index;
        }
        if ((stage.kind == GenericTubeAmpStageKind::Input && stage.id != "input") ||
            (stage.kind == GenericTubeAmpStageKind::Output && stage.id != "output") ||
            stage.model_reference != RequiredModel(stage.kind)) {
            return {.error = GenericTubeAmpCompileError::InvalidModelReference};
        }
        for (const double parameter : stage.parameters) {
            if (!std::isfinite(parameter)) return {.error = GenericTubeAmpCompileError::NonFinite};
        }
        if (stage.port_count != PortCount(stage.kind)) {
            return {.error = stage.port_count < PortCount(stage.kind)
                ? GenericTubeAmpCompileError::MissingPort : GenericTubeAmpCompileError::InvalidPort};
        }
        for (std::size_t port_index = 0U; port_index < stage.port_count; ++port_index) {
            const auto& port = stage.ports[port_index];
            const auto expected = Definition(stage.kind, port_index);
            if (port.id.empty() || port.node.empty() || port.pin.empty()) return {.error = GenericTubeAmpCompileError::InvalidPort};
            for (std::size_t prior = 0U; prior < port_index; ++prior) {
                if (stage.ports[prior].id == port.id) return {.error = GenericTubeAmpCompileError::DuplicatePort};
            }
            if (port.id != expected.id || port.pin != expected.pin || port.direction != expected.direction ||
                port.domain != expected.domain) return {.error = GenericTubeAmpCompileError::IncompatibleConnection};
        }
    }
    if (power_stage == kGenericTubeAmpMaximumStages || output_stage == kGenericTubeAmpMaximumStages) {
        return {.error = GenericTubeAmpCompileError::MissingStage};
    }
    for (std::size_t kind = 0U; kind < stage_seen.size(); ++kind) {
        if (kind == static_cast<std::size_t>(GenericTubeAmpStageKind::Pentode) ||
            kind == static_cast<std::size_t>(GenericTubeAmpStageKind::BeamTetrode) ||
            kind == static_cast<std::size_t>(GenericTubeAmpStageKind::Output)) continue;
        if (!stage_seen[kind]) return {.error = GenericTubeAmpCompileError::MissingStage};
    }

    const auto& supply = request.supplies[0];
    if (supply.id != "supply" || supply.model_reference != "generic-rectified-reservoir-v1" ||
        supply.positive_node.empty() || supply.negative_node.empty() || supply.positive_node == supply.negative_node) {
        return {.error = GenericTubeAmpCompileError::InvalidSupply};
    }
    for (const double parameter : supply.parameters) {
        if (!std::isfinite(parameter)) return {.error = GenericTubeAmpCompileError::NonFinite};
    }

    for (std::size_t index = 0U; index < request.component_count; ++index) {
        const auto& component = request.components[index];
        const auto definition = ComponentAt(index);
        if (component.id.empty() || component.model_reference.empty() || component.pin_count > kGenericTubeAmpMaximumComponentPins) {
            return {.error = GenericTubeAmpCompileError::InvalidComponentPin};
        }
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (request.components[prior].id == component.id) return {.error = GenericTubeAmpCompileError::DuplicateComponent};
        }
        if (component.id != definition.id || component.kind != definition.kind ||
            component.model_reference != definition.model_reference) {
            return {.error = GenericTubeAmpCompileError::InvalidModelReference};
        }
        if (component.pin_count != definition.pin_count) {
            return {.error = GenericTubeAmpCompileError::InvalidComponentPin};
        }
        for (const double parameter : component.parameters) {
            if (!std::isfinite(parameter)) return {.error = GenericTubeAmpCompileError::NonFinite};
        }
        for (std::size_t pin_index = 0U; pin_index < component.pin_count; ++pin_index) {
            const auto& pin = component.pins[pin_index];
            if (pin.id != ComponentPinId(component.kind, pin_index) || pin.node.empty()) {
                return {.error = GenericTubeAmpCompileError::InvalidComponentPin};
            }
            for (std::size_t prior = 0U; prior < pin_index; ++prior) {
                if (component.pins[prior].id == pin.id) return {.error = GenericTubeAmpCompileError::DuplicatePort};
            }
        }
    }

    for (std::size_t index = 0U; index < request.net_count; ++index) {
        const auto& net = request.nets[index];
        if (net.id.empty() || net.node.empty() || net.terminal_count > kGenericTubeAmpMaximumNetTerminals) {
            return {.error = net.terminal_count > kGenericTubeAmpMaximumNetTerminals
                ? GenericTubeAmpCompileError::CapacityExceeded : GenericTubeAmpCompileError::InvalidNetMembership};
        }
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (request.nets[prior].id == net.id) return {.error = GenericTubeAmpCompileError::DuplicateNet};
        }
    }

    for (std::size_t expected_index = 0U; expected_index < kRequiredNetCount; ++expected_index) {
        const auto expected = NetAt(expected_index);
        const auto net_index = FindNet(request, expected.id);
        if (net_index == kGenericTubeAmpMaximumNets) return {.error = GenericTubeAmpCompileError::MissingNet};
        const auto& net = request.nets[net_index];
        if (net.domain != expected.domain || net.terminal_count != expected.terminal_count) {
            return {.error = GenericTubeAmpCompileError::InvalidNetMembership};
        }
        for (std::size_t terminal_index = 0U; terminal_index < expected.terminal_count; ++terminal_index) {
            const auto& terminal = expected.terminals[terminal_index];
            if (!HasExpectedTerminal(net, terminal)) return {.error = GenericTubeAmpCompileError::InvalidNetMembership};
            const auto duplicate = std::count_if(net.terminals.begin(), net.terminals.begin() + net.terminal_count,
                [&terminal](const auto& item) { return item.owner == terminal.owner && item.owner_id == terminal.owner_id &&
                    item.terminal_id == terminal.terminal_id; });
            if (duplicate != 1) return {.error = GenericTubeAmpCompileError::DuplicateTerminal};
        }
        for (std::size_t terminal_index = 0U; terminal_index < net.terminal_count; ++terminal_index) {
            bool recognized = false;
            for (std::size_t expected_terminal = 0U; expected_terminal < expected.terminal_count; ++expected_terminal) {
                if (SameTerminal(net.terminals[terminal_index], expected.terminals[expected_terminal])) {
                    recognized = true;
                    break;
                }
            }
            if (!recognized) return {.error = GenericTubeAmpCompileError::UnknownTerminal};
        }
        for (std::size_t terminal_index = 0U; terminal_index < net.terminal_count; ++terminal_index) {
            const auto& terminal = net.terminals[terminal_index];
            std::string_view declared_node;
            GenericTubeAmpPortDomain declared_domain{};
            if (terminal.owner == GenericTubeAmpTerminalOwner::Stage) {
                const auto stage_index = FindStage(request, terminal.owner_id);
                if (stage_index == kGenericTubeAmpMaximumStages) return {.error = GenericTubeAmpCompileError::UnknownTerminal};
                const auto port_index = [&]() {
                    for (std::size_t p = 0U; p < request.stages[stage_index].port_count; ++p) {
                        if (request.stages[stage_index].ports[p].id == terminal.terminal_id) return p;
                    }
                    return kGenericTubeAmpMaximumComponentPins;
                }();
                if (port_index == kGenericTubeAmpMaximumComponentPins) return {.error = GenericTubeAmpCompileError::UnknownTerminal};
                declared_node = request.stages[stage_index].ports[port_index].node;
                declared_domain = request.stages[stage_index].ports[port_index].domain;
            } else {
                const auto component_index = FindComponent(request, terminal.owner_id);
                if (component_index == kGenericTubeAmpMaximumComponents) return {.error = GenericTubeAmpCompileError::UnknownTerminal};
                const auto pin_index = [&]() {
                    for (std::size_t p = 0U; p < request.components[component_index].pin_count; ++p) {
                        if (request.components[component_index].pins[p].id == terminal.terminal_id) return p;
                    }
                    return kGenericTubeAmpMaximumComponentPins;
                }();
                if (pin_index == kGenericTubeAmpMaximumComponentPins) return {.error = GenericTubeAmpCompileError::UnknownTerminal};
                declared_node = request.components[component_index].pins[pin_index].node;
                declared_domain = request.components[component_index].pins[pin_index].domain;
            }
            if (declared_node != net.node || !CompatibleNetDomain(net.domain, declared_domain, net.node)) {
                return {.error = GenericTubeAmpCompileError::IncompatibleConnection};
            }
        }
    }

    GenericTubeAmpCompilePlan candidate{};
    candidate.status = GenericTubeAmpPlanStatus::Ready;
    candidate.stage_count = request.stage_count;
    for (std::size_t index = 0U; index < request.stage_count; ++index) {
        const auto error = CopyStage(candidate.stages[index], request.stages[index]);
        if (error != GenericTubeAmpCompileError::None) return {.error = error};
    }
    candidate.component_count = request.component_count;
    for (std::size_t index = 0U; index < request.component_count; ++index) {
        const auto error = CopyComponent(candidate.components[index], request.components[index]);
        if (error != GenericTubeAmpCompileError::None) return {.error = error};
    }
    candidate.supply_count = request.supply_count;
    if (const auto error = CopySupply(candidate.supplies[0], request.supplies[0]);
        error != GenericTubeAmpCompileError::None) return {.error = error};
    candidate.net_count = request.net_count;
    for (std::size_t index = 0U; index < request.net_count; ++index) {
        const auto error = CopyNet(candidate.nets[index], request.nets[index]);
        if (error != GenericTubeAmpCompileError::None) return {.error = error};
    }
    return {.plan = candidate, .error = GenericTubeAmpCompileError::None};
}

} // namespace solo_rig
