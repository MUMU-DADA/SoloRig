#include "content/generic_tube_amp_mna_lowering.hpp"
#include "content/generic_tube_amp_mna_runtime.hpp"
#include "../fixtures/whitebox/m19b1_generic_tube_amp_compile_fixture.hpp"
#include "test_framework.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <type_traits>

namespace {

using solo_rig::GenericTubeAmpMnaLoweringError;

struct Inputs {
    solo_rig::GenericTubeAmpCompilePlan plan;
    solo_rig::CircuitNetlist netlist;
    solo_rig::ModelDatabase models;
};

Inputs MakeInputs() {
    const auto compiled = solo_rig::CompileGenericTubeAmp(
        solo_rig::fixtures::MakeMinimalGenericTubeAmpRequest());
    REQUIRE(compiled.plan.has_value());
    return {*compiled.plan, solo_rig::fixtures::MakeMinimalGenericTubeAmpCircuit(), {}};
}

void RequireLoweringError(Inputs& inputs, GenericTubeAmpMnaLoweringError error) {
    const auto result = solo_rig::LowerGenericTubeAmpToMna(inputs.plan, inputs.netlist, inputs.models);
    REQUIRE(!result.circuit.has_value());
    REQUIRE(result.error == error);
}

} // namespace

TEST_CASE("M19-B1 stage 3a validates the complete caller-owned MNA mapping") {
    auto inputs = MakeInputs();
    const auto result = solo_rig::LowerGenericTubeAmpToMna(inputs.plan, inputs.netlist, inputs.models);
    REQUIRE(result.circuit.has_value());
    REQUIRE(result.error == GenericTubeAmpMnaLoweringError::None);
}

TEST_CASE("M19-B1 stage 3a rejects node, pin, polarity and output mismatches atomically") {
    auto node = MakeInputs();
    node.netlist.components[0].pins[0] = "virtual_node";
    RequireLoweringError(node, GenericTubeAmpMnaLoweringError::InvalidPinOrder);

    auto transformer = MakeInputs();
    transformer.netlist.components[11].pins[1] = "power_plate";
    RequireLoweringError(transformer, GenericTubeAmpMnaLoweringError::InvalidPinOrder);

    auto speaker = MakeInputs();
    speaker.netlist.ports[1].node = "secondary_return";
    RequireLoweringError(speaker, GenericTubeAmpMnaLoweringError::InvalidPort);
}

TEST_CASE("M19-B1 stage 3a rejects missing, extra and wrong-family components") {
    auto missing = MakeInputs();
    missing.netlist.components.pop_back();
    RequireLoweringError(missing, GenericTubeAmpMnaLoweringError::MissingComponent);

    auto extra = MakeInputs();
    extra.netlist.components.push_back(extra.netlist.components.back());
    extra.netlist.components.back().id = "unauthorized_extra";
    RequireLoweringError(extra, GenericTubeAmpMnaLoweringError::ExtraComponent);

    auto wrong = MakeInputs();
    wrong.netlist.components[1].type = solo_rig::CircuitComponentType::Triode;
    RequireLoweringError(wrong, GenericTubeAmpMnaLoweringError::InvalidPinOrder);

    auto model = MakeInputs();
    model.netlist.components[0].triode_model->kind = "commercial-model";
    RequireLoweringError(model, GenericTubeAmpMnaLoweringError::MnaCompileFailure);
}

TEST_CASE("M19-B1 stage 3a rejects unready plans and bad supplies") {
    auto unready = MakeInputs();
    unready.plan.status = solo_rig::GenericTubeAmpPlanStatus::Empty;
    RequireLoweringError(unready, GenericTubeAmpMnaLoweringError::PlanNotReady);

    auto supply = MakeInputs();
    supply.netlist.supplies[0].negative_node = "power_plate";
    RequireLoweringError(supply, GenericTubeAmpMnaLoweringError::InvalidSupply);
}

TEST_CASE("M19-B1 stage 3b rejects a restored floating secondary return") {
    auto floating = MakeInputs();
    floating.netlist.nodes.push_back("secondary_return");
    floating.netlist.components[11].pins[3] = "secondary_return";
    floating.netlist.components[12].pins[1] = "secondary_return";
    const auto result = solo_rig::LowerGenericTubeAmpToMna(
        floating.plan, floating.netlist, floating.models);
    REQUIRE(!result.circuit.has_value());
    REQUIRE(result.error == GenericTubeAmpMnaLoweringError::NetlistMismatch ||
            result.error == GenericTubeAmpMnaLoweringError::MnaCompileFailure);
}

TEST_CASE("M19-B1 stage 3b runtime solves finite deterministic sequences") {
    auto inputs = MakeInputs();
    solo_rig::GenericTubeAmpMnaRuntime runtime;
    constexpr std::array<double, 4U> sequence{0.0, 1.0e-3, -1.0e-3, 2.0e-3};
    for (const double sample_rate : {48000.0, 96000.0, 192000.0}) {
        for (const std::uint32_t oversampling : {1U, 4U}) {
            auto rate_inputs = MakeInputs();
            rate_inputs.netlist.solver.oversampling = oversampling;
            REQUIRE(runtime.Prepare(rate_inputs.plan, rate_inputs.netlist, rate_inputs.models, sample_rate));
            REQUIRE(runtime.IsPrepared());
            std::array<double, sequence.size()> first{};
            for (std::size_t index = 0U; index < sequence.size(); ++index) {
                const auto result = runtime.Process(sequence[index]);
                REQUIRE(result.Succeeded());
                REQUIRE(std::isfinite(result.output_voltage));
                first[index] = result.output_voltage;
            }
            REQUIRE(std::isfinite(first[1]));
            REQUIRE(first[1] != first[2]);
            runtime.Reset();
            for (std::size_t index = 0U; index < sequence.size(); ++index) {
                const auto result = runtime.Process(sequence[index]);
                REQUIRE(result.Succeeded());
                REQUIRE(result.output_voltage == first[index]);
            }
        }
    }

    auto nonfinite = MakeInputs();
    REQUIRE(runtime.Prepare(nonfinite.plan, nonfinite.netlist, nonfinite.models, 48000.0));
    REQUIRE(runtime.Process(std::numeric_limits<double>::quiet_NaN()).error ==
            solo_rig::MnaSolveError::NonFinite);

    auto bad = MakeInputs();
    bad.netlist.components[0].pins[0] = "bad_node";
    REQUIRE(!runtime.Prepare(bad.plan, bad.netlist, bad.models, 48000.0));
    REQUIRE(!runtime.IsPrepared());
    REQUIRE(runtime.Process(0.0).error == solo_rig::MnaSolveError::NotPrepared);

    auto invalid_rate = MakeInputs();
    REQUIRE(!runtime.Prepare(invalid_rate.plan, invalid_rate.netlist, invalid_rate.models, 0.0));
    REQUIRE(!runtime.IsPrepared());
    REQUIRE(!runtime.Prepare(invalid_rate.plan, invalid_rate.netlist, invalid_rate.models,
                             std::numeric_limits<double>::quiet_NaN()));
    REQUIRE(!runtime.IsPrepared());
}

TEST_CASE("M19-B1 stage 3a exposes the existing realtime contract") {
    static_assert(noexcept(std::declval<solo_rig::GenericTubeAmpMnaRuntime&>().Process(0.0)));
    static_assert(noexcept(std::declval<solo_rig::GenericTubeAmpMnaRuntime&>().Reset()));
    static_assert(std::is_same_v<decltype(solo_rig::GenericTubeAmpMnaRuntime::ProcessContract()),
        solo_rig::RealtimeContract>);
    REQUIRE(solo_rig::GenericTubeAmpMnaRuntime::ProcessContract().process_allocates == false);
    REQUIRE(solo_rig::GenericTubeAmpMnaRuntime::ProcessContract().process_reads_files == false);
}
