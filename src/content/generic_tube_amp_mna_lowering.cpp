#include "content/generic_tube_amp_mna_lowering.hpp"

#include <algorithm>

namespace solo_rig {
namespace {

const GenericTubeAmpStagePlan* FindStage(const GenericTubeAmpCompilePlan& plan,
                                         std::string_view id) noexcept {
    for (std::size_t index = 0U; index < plan.stage_count; ++index) {
        if (plan.stages[index].id.View() == id) return &plan.stages[index];
    }
    return nullptr;
}

const GenericTubeAmpComponentPlan* FindComponent(const GenericTubeAmpCompilePlan& plan,
                                                 std::string_view id) noexcept {
    for (std::size_t index = 0U; index < plan.component_count; ++index) {
        if (plan.components[index].id.View() == id) return &plan.components[index];
    }
    return nullptr;
}

const CircuitComponent* FindCircuitComponent(const CircuitNetlist& netlist,
                                             std::string_view id) noexcept {
    for (const auto& component : netlist.components) {
        if (component.id == id) return &component;
    }
    return nullptr;
}

bool PinsMatch(const GenericTubeAmpStagePlan& stage, const CircuitComponent& component) noexcept {
    if (component.pins.size() != stage.port_count) return false;
    for (std::size_t index = 0U; index < stage.port_count; ++index) {
        if (component.pins[index] != stage.ports[index].node.View()) return false;
    }
    return true;
}

bool PinsMatch(const GenericTubeAmpComponentPlan& expected,
               const CircuitComponent& component) noexcept {
    if (component.pins.size() != expected.pin_count) return false;
    for (std::size_t index = 0U; index < expected.pin_count; ++index) {
        if (component.pins[index] != expected.pins[index].node.View()) return false;
    }
    return true;
}

bool ContainsNode(const CircuitNetlist& netlist, std::string_view node) noexcept {
    return std::find(netlist.nodes.begin(), netlist.nodes.end(), node) != netlist.nodes.end();
}

} // namespace

GenericTubeAmpMnaLoweringResult LowerGenericTubeAmpToMna(
    const GenericTubeAmpCompilePlan& plan,
    const CircuitNetlist& netlist,
    const ModelDatabase& model_database) {
    if (plan.status != GenericTubeAmpPlanStatus::Ready || plan.stage_count != kGenericTubeAmpMaximumStages ||
        plan.component_count != 11U || plan.supply_count != 1U || plan.net_count != kGenericTubeAmpMaximumNets) {
        return {.error = GenericTubeAmpMnaLoweringError::PlanNotReady};
    }
    if (netlist.components.size() < 13U) {
        return {.error = GenericTubeAmpMnaLoweringError::MissingComponent};
    }
    if (netlist.components.size() > 13U) {
        return {.error = GenericTubeAmpMnaLoweringError::ExtraComponent};
    }
    if (netlist.reference_ground != "ground" || netlist.nodes.size() != 11U ||
        netlist.ports.size() != 2U || netlist.supplies.size() != 1U) {
        return {.error = GenericTubeAmpMnaLoweringError::NetlistMismatch};
    }
    for (std::size_t index = 0U; index < plan.net_count; ++index) {
        if (!ContainsNode(netlist, plan.nets[index].node.View())) {
            return {.error = GenericTubeAmpMnaLoweringError::NetlistMismatch};
        }
    }
    const auto* input = FindStage(plan, "input");
    const auto* triode = FindStage(plan, "triode");
    const auto* power = FindStage(plan, "power");
    const auto* supply_stage = FindStage(plan, "supply");
    const auto* transformer = FindStage(plan, "transformer");
    const auto* speaker = FindStage(plan, "speaker");
    const auto* output = FindStage(plan, "output");
    if (input == nullptr || triode == nullptr || power == nullptr || supply_stage == nullptr ||
        transformer == nullptr || speaker == nullptr || output == nullptr) {
        return {.error = GenericTubeAmpMnaLoweringError::PlanNotReady};
    }
    const auto input_port = std::find_if(netlist.ports.begin(), netlist.ports.end(),
        [](const CircuitPort& port) { return port.id == "input" && port.direction == CircuitPortDirection::Input; });
    const auto output_port = std::find_if(netlist.ports.begin(), netlist.ports.end(),
        [](const CircuitPort& port) { return port.id == "output" && port.direction == CircuitPortDirection::Output; });
    if (input_port == netlist.ports.end() || output_port == netlist.ports.end() ||
        input_port->node != input->ports[0].node.View() || output_port->node != output->ports[0].node.View()) {
        return {.error = GenericTubeAmpMnaLoweringError::InvalidPort};
    }
    const auto& supply = netlist.supplies;
    if (supply.size() != 1U || supply[0].id != "supply" || supply[0].positive_node != plan.supplies[0].positive_node.View() ||
        supply[0].negative_node != plan.supplies[0].negative_node.View() || supply[0].kind != "rectified-reservoir") {
        return {.error = GenericTubeAmpMnaLoweringError::InvalidSupply};
    }
    const auto* triode_component = FindCircuitComponent(netlist, "triode");
    const auto* power_component = FindCircuitComponent(netlist, "power");
    const auto* transformer_component = FindCircuitComponent(netlist, "transformer_core");
    const auto* speaker_component = FindCircuitComponent(netlist, "speaker_load");
    const auto* transformer_expected = FindComponent(plan, "transformer_core");
    const auto* speaker_expected = FindComponent(plan, "speaker_load");
    if (triode_component == nullptr || power_component == nullptr || transformer_component == nullptr ||
        speaker_component == nullptr || transformer_expected == nullptr || speaker_expected == nullptr) {
        return {.error = GenericTubeAmpMnaLoweringError::MissingComponent};
    }
    if (triode_component->type != CircuitComponentType::Triode || !PinsMatch(*triode, *triode_component) ||
        (power->kind == GenericTubeAmpStageKind::Pentode && power_component->type != CircuitComponentType::Pentode) ||
        (power->kind == GenericTubeAmpStageKind::BeamTetrode && power_component->type != CircuitComponentType::BeamTetrode) ||
        !PinsMatch(*power, *power_component) || transformer_component->type != CircuitComponentType::CoupledInductor ||
        !PinsMatch(*transformer_expected, *transformer_component) ||
        speaker_component->type != CircuitComponentType::SpeakerLoad ||
        !PinsMatch(*speaker_expected, *speaker_component)) {
        return {.error = GenericTubeAmpMnaLoweringError::InvalidPinOrder};
    }
    constexpr std::array<std::string_view, 9U> passive_ids{
        "input_coupling", "triode_grid_leak", "triode_cathode_path", "triode_plate_load",
        "interstage_coupling", "power_grid_bias", "power_cathode_path", "screen_feed", "power_plate_load"};
    for (const auto id : passive_ids) {
        const auto* expected = FindComponent(plan, id);
        const auto* actual = FindCircuitComponent(netlist, id);
        if (expected == nullptr) return {.error = GenericTubeAmpMnaLoweringError::PlanNotReady};
        if (actual == nullptr) return {.error = GenericTubeAmpMnaLoweringError::MissingComponent};
        const auto expected_type = expected->kind == GenericTubeAmpComponentKind::Capacitor
            ? CircuitComponentType::Capacitor : CircuitComponentType::Resistor;
        if (actual->type != expected_type || !PinsMatch(*expected, *actual)) {
            return {.error = GenericTubeAmpMnaLoweringError::InvalidPinOrder};
        }
    }
    const auto compiled = CompileMnaCircuit(netlist, model_database);
    if (!compiled.circuit.has_value()) return {.error = GenericTubeAmpMnaLoweringError::MnaCompileFailure};
    return {.circuit = *compiled.circuit, .error = GenericTubeAmpMnaLoweringError::None};
}

} // namespace solo_rig
