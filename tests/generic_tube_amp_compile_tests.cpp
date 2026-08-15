#include "content/generic_tube_amp_compile.hpp"
#include "../fixtures/whitebox/m19b1_generic_tube_amp_compile_fixture.hpp"
#include "test_framework.hpp"

#include <array>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

using solo_rig::GenericTubeAmpCompileError;
using solo_rig::GenericTubeAmpComponentKind;
using solo_rig::GenericTubeAmpStageKind;
using solo_rig::GenericTubeAmpTerminalOwner;

solo_rig::GenericTubeAmpCompileRequest Request() {
    return solo_rig::fixtures::MakeMinimalGenericTubeAmpRequest();
}

void RequireRejected(const solo_rig::GenericTubeAmpCompileRequest& request,
                     GenericTubeAmpCompileError error) {
    const auto result = solo_rig::CompileGenericTubeAmp(request);
    REQUIRE(!result.plan.has_value());
    REQUIRE(result.error == error);
}

} // namespace

TEST_CASE("M19-B1 stage 2b compiles the physical typed terminal contract") {
    const auto result = solo_rig::CompileGenericTubeAmp(Request());
    REQUIRE(result.error == GenericTubeAmpCompileError::None);
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->status == solo_rig::GenericTubeAmpPlanStatus::Ready);
    REQUIRE(result.plan->stage_count == 8U);
    REQUIRE(result.plan->component_count == 11U);
    REQUIRE(result.plan->supply_count == 1U);
    REQUIRE(result.plan->net_count == 11U);
    REQUIRE(result.plan->stages[3].port_count == 4U);
    REQUIRE(result.plan->stages[3].ports[0].pin.View() == "plate");
    REQUIRE(result.plan->stages[5].port_count == 4U);
    REQUIRE(result.plan->stages[5].ports[0].pin.View() == "primary_dot");
    REQUIRE(result.plan->stages[6].port_count == 2U);
    REQUIRE(result.plan->stages[7].ports[0].node.View() == "secondary_dot");
    REQUIRE(result.plan->components[9].kind == GenericTubeAmpComponentKind::CoupledInductor);
    REQUIRE(result.plan->components[9].pin_count == 4U);
    REQUIRE(result.plan->components[10].pin_count == 2U);
}

TEST_CASE("M19-B1 stage 2b removes virtual power and speaker ports") {
    auto power_supply_pin = Request();
    power_supply_pin.stages[3].port_count = 5U;
    power_supply_pin.stages[3].ports[4] = power_supply_pin.stages[3].ports[3];
    power_supply_pin.stages[3].ports[4].id = "plate_supply";
    power_supply_pin.stages[3].ports[4].pin = "plate_supply";
    RequireRejected(power_supply_pin, GenericTubeAmpCompileError::CapacityExceeded);

    auto speaker_output = Request();
    speaker_output.stages[6].port_count = 3U;
    speaker_output.stages[6].ports[2] = {
        .id = "output", .node = "secondary_dot", .pin = "output",
        .direction = solo_rig::GenericTubeAmpPortDirection::Output,
        .domain = solo_rig::GenericTubeAmpPortDomain::TransformerSecondary};
    RequireRejected(speaker_output, GenericTubeAmpCompileError::InvalidPort);

    auto fake_supply_output = Request();
    fake_supply_output.stages[4].ports[0].id = "plate_supply";
    fake_supply_output.stages[4].ports[0].pin = "plate_supply";
    RequireRejected(fake_supply_output, GenericTubeAmpCompileError::IncompatibleConnection);
}

TEST_CASE("M19-B1 stage 2b enforces real transformer and speaker pin order") {
    auto incomplete_transformer = Request();
    incomplete_transformer.stages[5].port_count = 3U;
    RequireRejected(incomplete_transformer, GenericTubeAmpCompileError::MissingPort);

    auto wrong_polarity = Request();
    wrong_polarity.stages[5].ports[2].id = "secondary_return";
    wrong_polarity.stages[5].ports[2].pin = "secondary_return";
    RequireRejected(wrong_polarity, GenericTubeAmpCompileError::IncompatibleConnection);

    auto component_pin = Request();
    component_pin.components[9].pins[0].id = "primary";
    RequireRejected(component_pin, GenericTubeAmpCompileError::InvalidComponentPin);

    auto speaker_pin = Request();
    speaker_pin.components[10].pin_count = 3U;
    RequireRejected(speaker_pin, GenericTubeAmpCompileError::InvalidComponentPin);
}

TEST_CASE("M19-B1 stage 2b accepts declared multi-terminal supply and output measurement nets") {
    const auto result = solo_rig::CompileGenericTubeAmp(Request());
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->nets[6].terminal_count == 5U);
    REQUIRE(result.plan->nets[6].terminals[0].owner == GenericTubeAmpTerminalOwner::Stage);
    REQUIRE(result.plan->nets[9].terminal_count == 5U);
    REQUIRE(result.plan->nets[9].terminals[2].owner_id.View() == "output");
    REQUIRE(result.plan->nets[9].terminals[2].terminal_id.View() == "audio");
    REQUIRE(result.plan->nets[10].id.View() == "ground");
    REQUIRE(result.plan->nets[10].terminal_count == 9U);
}

TEST_CASE("M19-B1 stage 2b rejects undeclared terminals, missing membership and shorts") {
    auto extra_terminal = Request();
    extra_terminal.nets[6].terminal_count = 6U;
    extra_terminal.nets[6].terminals[5] = {
        .owner = GenericTubeAmpTerminalOwner::Component,
        .owner_id = "speaker_load", .terminal_id = "positive"};
    RequireRejected(extra_terminal, GenericTubeAmpCompileError::InvalidNetMembership);

    auto missing_terminal = Request();
    missing_terminal.nets[9].terminal_count = 4U;
    RequireRejected(missing_terminal, GenericTubeAmpCompileError::InvalidNetMembership);

    auto duplicate_terminal = Request();
    duplicate_terminal.nets[6].terminals[4] = duplicate_terminal.nets[6].terminals[3];
    RequireRejected(duplicate_terminal, GenericTubeAmpCompileError::DuplicateTerminal);

    auto shorted = Request();
    shorted.nets[7].terminals[2] = {
        .owner = GenericTubeAmpTerminalOwner::Stage, .owner_id = "supply", .terminal_id = "positive"};
    RequireRejected(shorted, GenericTubeAmpCompileError::InvalidNetMembership);
}

TEST_CASE("M19-B1 stage 2b rejects node, domain and model mismatches atomically") {
    auto node_mismatch = Request();
    node_mismatch.stages[7].ports[0].node = "virtual_output_audio";
    RequireRejected(node_mismatch, GenericTubeAmpCompileError::IncompatibleConnection);

    auto domain_mismatch = Request();
    domain_mismatch.components[7].pins[1].domain = solo_rig::GenericTubeAmpPortDomain::Audio;
    RequireRejected(domain_mismatch, GenericTubeAmpCompileError::IncompatibleConnection);

    auto invalid_model = Request();
    invalid_model.components[9].model_reference = "commercial-transformer";
    RequireRejected(invalid_model, GenericTubeAmpCompileError::InvalidModelReference);

    auto nonfinite = Request();
    nonfinite.components[3].parameters[0] = std::numeric_limits<double>::quiet_NaN();
    RequireRejected(nonfinite, GenericTubeAmpCompileError::NonFinite);
}

TEST_CASE("M19-B1 stage 2b rejects capacity and remains fixed-capacity compile-only") {
    auto capacity = Request();
    capacity.component_count = solo_rig::kGenericTubeAmpMaximumComponents + 1U;
    RequireRejected(capacity, GenericTubeAmpCompileError::CapacityExceeded);
    capacity = Request();
    capacity.net_count = solo_rig::kGenericTubeAmpMaximumNets + 1U;
    RequireRejected(capacity, GenericTubeAmpCompileError::CapacityExceeded);

    static_assert(noexcept(solo_rig::CompileGenericTubeAmp(
        std::declval<const solo_rig::GenericTubeAmpCompileRequest&>())));
    static_assert(std::is_same_v<decltype(solo_rig::GenericTubeAmpCompilePlan::nets),
        std::array<solo_rig::GenericTubeAmpNetPlan, solo_rig::kGenericTubeAmpMaximumNets>>);
    const auto result = solo_rig::CompileGenericTubeAmp(Request());
    REQUIRE(result.plan.has_value());
}
