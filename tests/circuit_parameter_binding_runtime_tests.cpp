#include "content/module_runtime.hpp"
#include "dsp/mna_solver.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
#include <variant>

namespace {

solo_rig::ModulePackage RuntimePackage() {
    solo_rig::ModulePackage package;
    package.descriptor = {.module_id="org.solorig.d1a",.display_name="D1a",.api_version=1,
        .category=solo_rig::EffectCategory::AnalogCircuit,.logic_entry_point="logic.json",
        .ports={{.id="input",.direction=solo_rig::PortDirection::Input,.max_channels=1},
                {.id="output",.direction=solo_rig::PortDirection::Output,.max_channels=1}},
        .parameters={{.id="position",.minimum=0,.maximum=1,.default_value=0.25F}}};
    package.logic = solo_rig::CircuitNetlist{
        .version=2,.schema="circuit",.reference_ground="0",.nodes={"in","out","0"},
        .ports={{.id="input",.direction=solo_rig::CircuitPortDirection::Input,.node="in"},
                {.id="output",.direction=solo_rig::CircuitPortDirection::Output,.node="out"}},
        .components={{.id="pot",.type=solo_rig::CircuitComponentType::Potentiometer,.pins={"in","0","out"},
            .value=10000.0,.potentiometer=solo_rig::CircuitPotentiometerSpec{.position=.25,.minimum=0,.maximum=1}}},
        .parameter_bindings={{.parameter_id="position",.target_id="pot",
            .property=solo_rig::CircuitParameterBindingProperty::PotentiometerPosition}},.solver={.oversampling=4}};
    return package;
}

} // namespace

TEST_CASE("M18-D1a runtime publishes validated targets and applies them without recompile") {
    auto created=solo_rig::CreateModuleRuntime(RuntimePackage());
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(created));
    auto runtime=std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(created));
    solo_rig::AudioFormat format{.sample_rate_hz=192000,.max_block_size=8,.channel_count=1};
    REQUIRE(runtime->Prepare(format));
    solo_rig::AudioBuffer input(1,8),output(1,8); input.ChannelData(0)[0]=1.0F;
    runtime->Process(input,output,1); REQUIRE(std::fabs(output.ChannelData(0)[0]-.75F)<1e-5F);
    REQUIRE(runtime->SetParameter("position",.75F));
    REQUIRE(runtime->ParameterValue("position").has_value());
    runtime->Process(input,output,1); REQUIRE(std::fabs(output.ChannelData(0)[0]-.25F)<1e-5F);
    REQUIRE(!runtime->SetParameter("missing",.5F));
    REQUIRE(!runtime->SetParameter("position",NAN));
    REQUIRE(!runtime->SetParameter("position",2.0F));
}

TEST_CASE("M18-D1a runtime rejects descriptor ranges that exceed their bound target") {
    auto package=RuntimePackage(); package.descriptor.parameters.front().maximum=2.0F;
    REQUIRE(std::holds_alternative<solo_rig::ModuleRuntimeError>(solo_rig::CreateModuleRuntime(package)));
}

TEST_CASE("M18-D1a runtime mailbox accepts concurrent automation while Process remains finite") {
    auto created=solo_rig::CreateModuleRuntime(RuntimePackage());
    REQUIRE(std::holds_alternative<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(created));
    auto runtime=std::move(std::get<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>>(created));
    REQUIRE(runtime->Prepare({.sample_rate_hz=192000,.max_block_size=8,.channel_count=1}));
    solo_rig::AudioBuffer input(1,8),output(1,8);
    for (std::uint32_t sample=0;sample<8;++sample) input.ChannelData(0)[sample]=1.0F;
    std::atomic<bool> start{};
    std::atomic<bool> published{true};
    std::thread publisher([&] {
        while (!start.load(std::memory_order_acquire)) {}
        for (int index=0;index<1000;++index) {
            if (!runtime->SetParameter("position",(index&1)==0 ? .1F : .9F)) {
                published.store(false,std::memory_order_release);
            }
        }
    });
    start.store(true,std::memory_order_release);
    for (int block=0;block<100;++block) {
        runtime->Process(input,output,8);
        for (std::uint32_t sample=0;sample<8;++sample) REQUIRE(std::isfinite(output.ChannelData(0)[sample]));
    }
    publisher.join();
    REQUIRE(published.load(std::memory_order_acquire));
    constexpr auto contract=solo_rig::MnaPreparedCircuit::ProcessContract();
    static_assert(!contract.process_allocates && !contract.process_locks &&
                  !contract.process_reads_files && !contract.process_parses_json);
}

TEST_CASE("M18-D1a module loading rejects undeclared and out-of-target binding parameters") {
    const auto directory=std::filesystem::temp_directory_path()/"SoloRigD1aBindingLoad";
    std::error_code error; std::filesystem::remove_all(directory,error);
    std::filesystem::create_directories(directory/"logic",error);
    const auto write=[&](std::string_view parameters) {
        std::ofstream(directory/"manifest.json",std::ios::binary|std::ios::trunc)
            << R"({"schemaVersion":1,"moduleId":"org.solorig.d1a.load","displayName":"D1a","category":"analog-circuit","logicFile":"logic/main.json","ports":[{"id":"input","direction":"input","maxChannels":1},{"id":"output","direction":"output","maxChannels":1}],"parameters":)"
            << parameters << "}";
        std::ofstream(directory/"logic"/"main.json",std::ios::binary|std::ios::trunc) << R"({
          "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
          "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
          "components":[{"id":"pot","type":"potentiometer","pins":["in","0","out"],"value":10000,
            "potentiometer":{"position":0.5,"minimum":0,"maximum":1}}],
          "parameterBindings":[{"modelVersion":1,"parameter":"position","target":"pot",
            "property":"potentiometer.position","smoothingSeconds":0}],
          "solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":1}})";
    };
    write(R"([{"id":"other","minimum":0,"maximum":1,"default":0.5}])");
    auto missing=solo_rig::LoadModulePackage(directory);
    REQUIRE(!missing.package.has_value());
    REQUIRE(missing.errors.front()==solo_rig::ModulePackageLoadError::LogicParameterUnknown);
    write(R"([{"id":"position","minimum":-1,"maximum":2,"default":0.5}])");
    auto range=solo_rig::LoadModulePackage(directory);
    std::filesystem::remove_all(directory,error);
    REQUIRE(!range.package.has_value());
    REQUIRE(range.errors.front()==solo_rig::ModulePackageLoadError::InvalidLogicDefinition);
}
