#include "content/circuit_v2.hpp"
#include "content/module_runtime.hpp"
#include "dsp/mna_solver.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <memory>
#include <variant>

TEST_CASE("M18-D1b1 external relay runtime is preallocated and finite at 192 kHz 4x") {
    const auto parsed=solo_rig::ParseCircuitV2Json(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["coil","rail","out","0"],
      "ports":[{"id":"input","direction":"input","node":"coil"},{"id":"output","direction":"output","node":"out"}],
      "supplies":[{"id":"rail","positive":"rail","negative":"0","voltage":1}],
      "components":[{"id":"rly","type":"relay","pins":["coil","0","rail","out"],"relayModel":{
        "kind":"generic-dynamic","modelVersion":1,"coilResistance":100,"coilInductance":0.001,
        "openResistance":1000000,"closedResistance":10,"pullInCurrent":0.02,"dropOutCurrent":0.01,
        "operateTime":0.0001,"releaseTime":0.0002,"bounceDuration":0.001,"bounceFrequency":1000,
        "bounceAmplitude":0.1,"maximumCoilCurrent":1,"initialState":"open"}},
        {"id":"load","type":"resistor","pins":["out","0"],"value":1000}],
      "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4,
        "convergence":{"maxIterations":16,"absoluteTolerance":1e-10,"relativeTolerance":1e-8}}})");
    REQUIRE(parsed.circuit.has_value());
    solo_rig::ModulePackage package;
    package.descriptor={.module_id="org.solorig.relay",.display_name="Relay",.api_version=1,
        .category=solo_rig::EffectCategory::AnalogCircuit,.logic_entry_point="logic.json",
        .ports={{.id="input",.direction=solo_rig::PortDirection::Input,.max_channels=1},
                {.id="output",.direction=solo_rig::PortDirection::Output,.max_channels=1}}};
    package.logic=*parsed.circuit;
    auto created=solo_rig::CreateModuleRuntime(package);
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(created));
    auto runtime=std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(created));
    REQUIRE(runtime->Prepare({.sample_rate_hz=192000,.max_block_size=64,.channel_count=1}));
    solo_rig::AudioBuffer input(1,64),output(1,64);
    for(std::uint32_t sample=0;sample<64;++sample) input.ChannelData(0)[sample]=5.0F;
    for(int block=0;block<20;++block) {
        runtime->Process(input,output,64);
        for(std::uint32_t sample=0;sample<64;++sample) REQUIRE(std::isfinite(output.ChannelData(0)[sample]));
    }
    REQUIRE(output.ChannelData(0)[63]>0.9F);
    REQUIRE(!runtime->SetParameter("relay",1.0F));
    static_assert(solo_rig::IsRealtimeSafe(solo_rig::MnaPreparedCircuit::ProcessContract()));
}
