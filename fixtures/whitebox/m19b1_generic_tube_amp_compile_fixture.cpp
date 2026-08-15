#include "m19b1_generic_tube_amp_compile_fixture.hpp"

#include <initializer_list>
#include <utility>

namespace solo_rig::fixtures {
namespace {

using Stage = GenericTubeAmpStageRequest;
using Component = GenericTubeAmpComponentRequest;
using Owner = GenericTubeAmpTerminalOwner;

void SetPort(Stage& stage, std::size_t index, std::string_view id, std::string_view node,
             std::string_view pin, GenericTubeAmpPortDirection direction,
             GenericTubeAmpPortDomain domain) noexcept {
    stage.ports[index] = {.id = id, .node = node, .pin = pin, .direction = direction, .domain = domain};
}

Stage MakeStage(std::string_view id, std::string_view model, GenericTubeAmpStageKind kind,
                std::size_t port_count) noexcept {
    Stage stage{};
    stage.id = id;
    stage.model_reference = model;
    stage.kind = kind;
    stage.port_count = port_count;
    return stage;
}

void SetPin(Component& component, std::size_t index, std::string_view id,
            std::string_view node, GenericTubeAmpPortDomain domain) noexcept {
    component.pins[index] = {.id = id, .node = node, .domain = domain};
}

Component MakeComponent(std::string_view id, GenericTubeAmpComponentKind kind,
                        std::string_view model, std::size_t pin_count) noexcept {
    Component component{};
    component.id = id;
    component.kind = kind;
    component.model_reference = model;
    component.pin_count = pin_count;
    component.parameters[0] = 1.0;
    component.parameters[1] = 0.1;
    return component;
}

void SetNet(GenericTubeAmpNetRequest& net, std::string_view id, std::string_view node,
            GenericTubeAmpPortDomain domain) noexcept {
    net.id = id;
    net.node = node;
    net.domain = domain;
}

void AddTerminal(GenericTubeAmpNetRequest& net, GenericTubeAmpTerminalOwner owner,
                 std::string_view owner_id, std::string_view terminal_id) noexcept {
    net.terminals[net.terminal_count++] = {
        .owner = owner, .owner_id = owner_id, .terminal_id = terminal_id};
}

} // namespace

CircuitNetlist MakeMinimalGenericTubeAmpCircuit() {
    CircuitNetlist circuit{};
    circuit.reference_ground = "ground";
    circuit.nodes = {"input_audio", "triode_grid", "triode_plate", "power_grid", "triode_cathode",
        "power_cathode", "supply_rail", "power_plate", "power_screen", "secondary_dot",
        "ground"};
    circuit.ports = {
        {.id = "input", .direction = CircuitPortDirection::Input, .node = "input_audio"},
        {.id = "output", .direction = CircuitPortDirection::Output, .node = "secondary_dot"}};
    circuit.supplies = {{.id = "supply", .positive_node = "supply_rail", .negative_node = "ground",
        .voltage = 250.0, .kind = "rectified-reservoir", .model_version = 1U,
        .source_resistance = 10.0, .sag_resistance = 20.0, .recovery_time_seconds = 0.1,
        .ripple_amplitude = 0.0, .ripple_frequency_hz = 100.0, .maximum_sag = 20.0,
        .maximum_load_current = 1.0, .initial_sag = 0.0}};
    CircuitComponent triode{};
    triode.id = "triode";
    triode.type = CircuitComponentType::Triode;
    triode.pins = {"triode_plate", "triode_grid", "triode_cathode"};
    triode.triode_model = CircuitTriodeModel{};
    circuit.components.push_back(triode);
    CircuitComponent power{};
    power.id = "power";
    power.type = CircuitComponentType::Pentode;
    power.pins = {"power_plate", "power_screen", "power_grid", "power_cathode"};
    power.pentode_model = CircuitPentodeModel{};
    circuit.components.push_back(power);
    auto add_passive = [&circuit](std::string_view id, CircuitComponentType type,
                                  std::initializer_list<std::string_view> pins, double value) {
        CircuitComponent component{};
        component.id = id;
        component.type = type;
        for (const auto pin : pins) component.pins.emplace_back(pin);
        component.value = value;
        circuit.components.push_back(std::move(component));
    };
    add_passive("input_coupling", CircuitComponentType::Capacitor, {"input_audio", "triode_grid"}, 1.0e-6);
    add_passive("triode_grid_leak", CircuitComponentType::Resistor, {"triode_grid", "ground"}, 1.0e6);
    add_passive("triode_cathode_path", CircuitComponentType::Resistor, {"triode_cathode", "ground"}, 1.0e3);
    add_passive("triode_plate_load", CircuitComponentType::Resistor, {"supply_rail", "triode_plate"}, 100.0e3);
    add_passive("interstage_coupling", CircuitComponentType::Capacitor, {"triode_plate", "power_grid"}, 1.0e-6);
    add_passive("power_grid_bias", CircuitComponentType::Resistor, {"power_grid", "ground"}, 1.0e6);
    add_passive("power_cathode_path", CircuitComponentType::Resistor, {"power_cathode", "ground"}, 500.0);
    add_passive("screen_feed", CircuitComponentType::Resistor, {"supply_rail", "power_screen"}, 10.0e3);
    add_passive("power_plate_load", CircuitComponentType::Resistor, {"supply_rail", "power_plate"}, 10.0e3);
    CircuitComponent transformer{};
    transformer.id = "transformer_core";
    transformer.type = CircuitComponentType::CoupledInductor;
    transformer.pins = {"power_plate", "supply_rail", "secondary_dot", "ground"};
    transformer.coupled_inductor_model = CircuitCoupledInductorModel{
        .kind = "saturating-hysteretic", .model_version = 1U, .turns_ratio = 2.0,
        .magnetizing_inductance = 1.0, .primary_leakage_inductance = 0.1,
        .secondary_leakage_inductance = 0.025, .primary_resistance = 10.0,
        .secondary_resistance = 5.0, .saturation_flux = 1.0, .hysteresis_width = 0.1,
        .hysteresis_rate = 1000.0, .remanent_flux = 0.0, .max_flux = 2.0,
        .max_current = 10.0, .max_step = 1.0};
    circuit.components.push_back(transformer);
    CircuitComponent speaker{};
    speaker.id = "speaker_load";
    speaker.type = CircuitComponentType::SpeakerLoad;
    speaker.pins = {"secondary_dot", "ground"};
    speaker.speaker_load_model = CircuitSpeakerLoadModel{
        .kind = "electrodynamic", .model_version = 1U, .voice_coil_resistance = 8.0,
        .voice_coil_inductance = 1.0e-3, .force_factor = 5.0, .moving_mass = 0.02,
        .mechanical_resistance = 1.0, .compliance = 1.0e-4, .nonlinear_stiffness = 100.0,
        .nonlinear_displacement_scale = 1.0e-3, .maximum_current = 10.0,
        .maximum_velocity = 100.0, .maximum_displacement = 0.01,
        .initial_current = 0.0, .initial_velocity = 0.0, .initial_displacement = 0.0};
    circuit.components.push_back(speaker);
    circuit.solver.oversampling = 1U;
    circuit.solver.convergence.max_iterations = 256U;
    circuit.solver.convergence.absolute_tolerance = 1.0e-9;
    circuit.solver.convergence.relative_tolerance = 1.0e-6;
    return circuit;
}

GenericTubeAmpCompileRequest MakeMinimalGenericTubeAmpRequest() noexcept {
    GenericTubeAmpCompileRequest request{};
    request.stage_count = 8U;
    request.stages[0] = MakeStage("input", "external-audio-v1", GenericTubeAmpStageKind::Input, 1U);
    SetPort(request.stages[0], 0U, "audio", "input_audio", "input",
            GenericTubeAmpPortDirection::Output, GenericTubeAmpPortDomain::Audio);

    request.stages[1] = MakeStage("triode", "generic-triode-v1", GenericTubeAmpStageKind::Triode, 3U);
    SetPort(request.stages[1], 0U, "plate", "triode_plate", "plate",
            GenericTubeAmpPortDirection::Terminal, GenericTubeAmpPortDomain::Audio);
    SetPort(request.stages[1], 1U, "grid", "triode_grid", "grid",
            GenericTubeAmpPortDirection::Terminal, GenericTubeAmpPortDomain::Audio);
    SetPort(request.stages[1], 2U, "cathode", "triode_cathode", "cathode",
            GenericTubeAmpPortDirection::Terminal, GenericTubeAmpPortDomain::Bias);

    request.stages[2] = MakeStage("coupling", "generic-passive-coupling-v1",
        GenericTubeAmpStageKind::CouplingNetwork, 2U);
    SetPort(request.stages[2], 0U, "in", "triode_plate", "in",
            GenericTubeAmpPortDirection::Terminal, GenericTubeAmpPortDomain::Audio);
    SetPort(request.stages[2], 1U, "out", "power_grid", "out",
            GenericTubeAmpPortDirection::Terminal, GenericTubeAmpPortDomain::Audio);

    request.stages[3] = MakeStage("power", "generic-pentode-v1", GenericTubeAmpStageKind::Pentode, 4U);
    SetPort(request.stages[3], 0U, "plate", "power_plate", "plate",
            GenericTubeAmpPortDirection::Terminal, GenericTubeAmpPortDomain::TransformerPrimary);
    SetPort(request.stages[3], 1U, "screen", "power_screen", "screen",
            GenericTubeAmpPortDirection::Terminal, GenericTubeAmpPortDomain::SupplyRail);
    SetPort(request.stages[3], 2U, "grid", "power_grid", "grid",
            GenericTubeAmpPortDirection::Terminal, GenericTubeAmpPortDomain::Audio);
    SetPort(request.stages[3], 3U, "cathode", "power_cathode", "cathode",
            GenericTubeAmpPortDirection::Terminal, GenericTubeAmpPortDomain::Bias);

    request.stages[4] = MakeStage("supply", "generic-rectified-reservoir-v1",
        GenericTubeAmpStageKind::Supply, 2U);
    SetPort(request.stages[4], 0U, "positive", "supply_rail", "positive",
            GenericTubeAmpPortDirection::Terminal, GenericTubeAmpPortDomain::SupplyRail);
    SetPort(request.stages[4], 1U, "negative", "ground", "negative",
            GenericTubeAmpPortDirection::Terminal, GenericTubeAmpPortDomain::Ground);

    request.stages[5] = MakeStage("transformer", "generic-saturating-hysteretic-transformer-v1",
        GenericTubeAmpStageKind::Transformer, 4U);
    SetPort(request.stages[5], 0U, "primary_dot", "power_plate", "primary_dot",
            GenericTubeAmpPortDirection::Terminal, GenericTubeAmpPortDomain::TransformerPrimary);
    SetPort(request.stages[5], 1U, "primary_return", "supply_rail", "primary_return",
            GenericTubeAmpPortDirection::Terminal, GenericTubeAmpPortDomain::SupplyRail);
    SetPort(request.stages[5], 2U, "secondary_dot", "secondary_dot", "secondary_dot",
            GenericTubeAmpPortDirection::Terminal, GenericTubeAmpPortDomain::TransformerSecondary);
    SetPort(request.stages[5], 3U, "secondary_return", "ground", "secondary_return",
            GenericTubeAmpPortDirection::Terminal, GenericTubeAmpPortDomain::TransformerSecondary);

    request.stages[6] = MakeStage("speaker", "generic-electrodynamic-speaker-v1",
        GenericTubeAmpStageKind::Speaker, 2U);
    SetPort(request.stages[6], 0U, "positive", "secondary_dot", "positive",
            GenericTubeAmpPortDirection::Terminal, GenericTubeAmpPortDomain::TransformerSecondary);
    SetPort(request.stages[6], 1U, "negative", "ground", "negative",
            GenericTubeAmpPortDirection::Terminal, GenericTubeAmpPortDomain::TransformerSecondary);

    request.stages[7] = MakeStage("output", "external-audio-v1", GenericTubeAmpStageKind::Output, 1U);
    SetPort(request.stages[7], 0U, "audio", "secondary_dot", "output",
            GenericTubeAmpPortDirection::Input, GenericTubeAmpPortDomain::TransformerSecondary);

    request.component_count = 11U;
    request.components[0] = MakeComponent("input_coupling", GenericTubeAmpComponentKind::Capacitor,
        "generic-capacitor-v1", 2U);
    SetPin(request.components[0], 0U, "positive", "input_audio", GenericTubeAmpPortDomain::Audio);
    SetPin(request.components[0], 1U, "negative", "triode_grid", GenericTubeAmpPortDomain::Audio);

    request.components[1] = MakeComponent("triode_grid_leak", GenericTubeAmpComponentKind::Resistor,
        "generic-resistor-v1", 2U);
    SetPin(request.components[1], 0U, "positive", "triode_grid", GenericTubeAmpPortDomain::Audio);
    SetPin(request.components[1], 1U, "negative", "ground", GenericTubeAmpPortDomain::Ground);

    request.components[2] = MakeComponent("triode_cathode_path", GenericTubeAmpComponentKind::Resistor,
        "generic-resistor-v1", 2U);
    SetPin(request.components[2], 0U, "positive", "triode_cathode", GenericTubeAmpPortDomain::Bias);
    SetPin(request.components[2], 1U, "negative", "ground", GenericTubeAmpPortDomain::Ground);

    request.components[3] = MakeComponent("triode_plate_load", GenericTubeAmpComponentKind::Resistor,
        "generic-resistor-v1", 2U);
    SetPin(request.components[3], 0U, "positive", "supply_rail", GenericTubeAmpPortDomain::SupplyRail);
    SetPin(request.components[3], 1U, "negative", "triode_plate", GenericTubeAmpPortDomain::Audio);

    request.components[4] = MakeComponent("interstage_coupling", GenericTubeAmpComponentKind::Capacitor,
        "generic-capacitor-v1", 2U);
    SetPin(request.components[4], 0U, "positive", "triode_plate", GenericTubeAmpPortDomain::Audio);
    SetPin(request.components[4], 1U, "negative", "power_grid", GenericTubeAmpPortDomain::Audio);

    request.components[5] = MakeComponent("power_grid_bias", GenericTubeAmpComponentKind::Resistor,
        "generic-resistor-v1", 2U);
    SetPin(request.components[5], 0U, "positive", "power_grid", GenericTubeAmpPortDomain::Audio);
    SetPin(request.components[5], 1U, "negative", "ground", GenericTubeAmpPortDomain::Ground);

    request.components[6] = MakeComponent("power_cathode_path", GenericTubeAmpComponentKind::Resistor,
        "generic-resistor-v1", 2U);
    SetPin(request.components[6], 0U, "positive", "power_cathode", GenericTubeAmpPortDomain::Bias);
    SetPin(request.components[6], 1U, "negative", "ground", GenericTubeAmpPortDomain::Ground);

    request.components[7] = MakeComponent("screen_feed", GenericTubeAmpComponentKind::Resistor,
        "generic-resistor-v1", 2U);
    SetPin(request.components[7], 0U, "positive", "supply_rail", GenericTubeAmpPortDomain::SupplyRail);
    SetPin(request.components[7], 1U, "negative", "power_screen", GenericTubeAmpPortDomain::SupplyRail);

    request.components[8] = MakeComponent("power_plate_load", GenericTubeAmpComponentKind::Resistor,
        "generic-resistor-v1", 2U);
    SetPin(request.components[8], 0U, "positive", "supply_rail", GenericTubeAmpPortDomain::SupplyRail);
    SetPin(request.components[8], 1U, "negative", "power_plate", GenericTubeAmpPortDomain::TransformerPrimary);

    request.components[9] = MakeComponent("transformer_core", GenericTubeAmpComponentKind::CoupledInductor,
        "generic-saturating-hysteretic-transformer-v1", 4U);
    SetPin(request.components[9], 0U, "primary_dot", "power_plate", GenericTubeAmpPortDomain::TransformerPrimary);
    SetPin(request.components[9], 1U, "primary_return", "supply_rail", GenericTubeAmpPortDomain::SupplyRail);
    SetPin(request.components[9], 2U, "secondary_dot", "secondary_dot", GenericTubeAmpPortDomain::TransformerSecondary);
    SetPin(request.components[9], 3U, "secondary_return", "ground", GenericTubeAmpPortDomain::TransformerSecondary);

    request.components[10] = MakeComponent("speaker_load", GenericTubeAmpComponentKind::SpeakerLoad,
        "generic-electrodynamic-speaker-v1", 2U);
    SetPin(request.components[10], 0U, "positive", "secondary_dot", GenericTubeAmpPortDomain::TransformerSecondary);
    SetPin(request.components[10], 1U, "negative", "ground", GenericTubeAmpPortDomain::TransformerSecondary);

    request.supply_count = 1U;
    request.supplies[0] = {
        .id = "supply", .model_reference = "generic-rectified-reservoir-v1",
        .positive_node = "supply_rail", .negative_node = "ground",
        .parameters = {250.0, 10.0, 20.0, 0.1, 0.0, 100.0, 20.0, 1.0, 0.0}};

    request.net_count = 11U;
    SetNet(request.nets[0], "input_audio", "input_audio", GenericTubeAmpPortDomain::Audio);
    AddTerminal(request.nets[0], Owner::Stage, "input", "audio");
    AddTerminal(request.nets[0], Owner::Component, "input_coupling", "positive");
    SetNet(request.nets[1], "triode_grid", "triode_grid", GenericTubeAmpPortDomain::Audio);
    AddTerminal(request.nets[1], Owner::Stage, "triode", "grid");
    AddTerminal(request.nets[1], Owner::Component, "input_coupling", "negative");
    AddTerminal(request.nets[1], Owner::Component, "triode_grid_leak", "positive");
    SetNet(request.nets[2], "triode_plate", "triode_plate", GenericTubeAmpPortDomain::Audio);
    AddTerminal(request.nets[2], Owner::Stage, "triode", "plate");
    AddTerminal(request.nets[2], Owner::Stage, "coupling", "in");
    AddTerminal(request.nets[2], Owner::Component, "triode_plate_load", "negative");
    AddTerminal(request.nets[2], Owner::Component, "interstage_coupling", "positive");
    SetNet(request.nets[3], "power_grid", "power_grid", GenericTubeAmpPortDomain::Audio);
    AddTerminal(request.nets[3], Owner::Stage, "power", "grid");
    AddTerminal(request.nets[3], Owner::Stage, "coupling", "out");
    AddTerminal(request.nets[3], Owner::Component, "interstage_coupling", "negative");
    AddTerminal(request.nets[3], Owner::Component, "power_grid_bias", "positive");
    SetNet(request.nets[4], "triode_cathode", "triode_cathode", GenericTubeAmpPortDomain::Bias);
    AddTerminal(request.nets[4], Owner::Stage, "triode", "cathode");
    AddTerminal(request.nets[4], Owner::Component, "triode_cathode_path", "positive");
    SetNet(request.nets[5], "power_cathode", "power_cathode", GenericTubeAmpPortDomain::Bias);
    AddTerminal(request.nets[5], Owner::Stage, "power", "cathode");
    AddTerminal(request.nets[5], Owner::Component, "power_cathode_path", "positive");
    SetNet(request.nets[6], "supply_rail", "supply_rail", GenericTubeAmpPortDomain::SupplyRail);
    AddTerminal(request.nets[6], Owner::Stage, "supply", "positive");
    AddTerminal(request.nets[6], Owner::Component, "triode_plate_load", "positive");
    AddTerminal(request.nets[6], Owner::Component, "screen_feed", "positive");
    AddTerminal(request.nets[6], Owner::Component, "power_plate_load", "positive");
    AddTerminal(request.nets[6], Owner::Stage, "transformer", "primary_return");
    SetNet(request.nets[7], "power_plate", "power_plate", GenericTubeAmpPortDomain::TransformerPrimary);
    AddTerminal(request.nets[7], Owner::Stage, "power", "plate");
    AddTerminal(request.nets[7], Owner::Stage, "transformer", "primary_dot");
    AddTerminal(request.nets[7], Owner::Component, "power_plate_load", "negative");
    SetNet(request.nets[8], "power_screen", "power_screen", GenericTubeAmpPortDomain::SupplyRail);
    AddTerminal(request.nets[8], Owner::Stage, "power", "screen");
    AddTerminal(request.nets[8], Owner::Component, "screen_feed", "negative");
    SetNet(request.nets[9], "secondary_dot", "secondary_dot", GenericTubeAmpPortDomain::TransformerSecondary);
    AddTerminal(request.nets[9], Owner::Stage, "transformer", "secondary_dot");
    AddTerminal(request.nets[9], Owner::Stage, "speaker", "positive");
    AddTerminal(request.nets[9], Owner::Stage, "output", "audio");
    AddTerminal(request.nets[9], Owner::Component, "transformer_core", "secondary_dot");
    AddTerminal(request.nets[9], Owner::Component, "speaker_load", "positive");
    SetNet(request.nets[10], "ground", "ground", GenericTubeAmpPortDomain::Ground);
    AddTerminal(request.nets[10], Owner::Stage, "supply", "negative");
    AddTerminal(request.nets[10], Owner::Component, "triode_grid_leak", "negative");
    AddTerminal(request.nets[10], Owner::Component, "triode_cathode_path", "negative");
    AddTerminal(request.nets[10], Owner::Component, "power_grid_bias", "negative");
    AddTerminal(request.nets[10], Owner::Component, "power_cathode_path", "negative");
    AddTerminal(request.nets[10], Owner::Stage, "transformer", "secondary_return");
    AddTerminal(request.nets[10], Owner::Stage, "speaker", "negative");
    AddTerminal(request.nets[10], Owner::Component, "transformer_core", "secondary_return");
    AddTerminal(request.nets[10], Owner::Component, "speaker_load", "negative");
    return request;
}

} // namespace solo_rig::fixtures
