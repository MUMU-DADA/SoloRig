#include "content/module_runtime.hpp"
#include "core/rig_runtime.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <atomic>
#include <memory>
#include <string_view>

namespace {

solo_rig::AudioFormat TestFormat() {
    return {.sample_rate_hz = 48000.0, .max_block_size = 64, .channel_count = 1};
}

class GainProcessor final : public solo_rig::ModuleRuntimeProcessor {
public:
    explicit GainProcessor(float gain) : gain_(gain) {}

    bool Prepare(const solo_rig::AudioFormat& format) override {
        format_ = format;
        return format.Validate().empty();
    }

    void Process(const solo_rig::AudioBuffer& input,
                 solo_rig::AudioBuffer& output,
                 std::uint32_t sample_count) noexcept override {
        for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
            output.ChannelData(0)[sample] = input.ChannelData(0)[sample] * gain_;
        }
    }

    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override { return latency_; }

    [[nodiscard]] bool SetParameter(std::string_view parameter_id, float value) noexcept override {
        if (parameter_id != "gain" || value < 0.0F || value > 2.0F) {
            return false;
        }
        gain_ = value;
        return true;
    }

    [[nodiscard]] std::optional<float> ParameterValue(std::string_view parameter_id) const noexcept override {
        return parameter_id == "gain" ? std::optional<float>(gain_) : std::nullopt;
    }

    void SetLatency(std::uint32_t latency) noexcept { latency_ = latency; }

private:
    solo_rig::AudioFormat format_{};
    float gain_{};
    std::uint32_t latency_{};
};

solo_rig::RigPreset MakeGainPreset(std::string name, float gain) {
    return {.name = std::move(name),
            .nodes = {{.instance_id = "gain", .module_id = "test.gain", .parameter_values = {{"gain", gain}}}}};
}

solo_rig::PreparedPreset Prepare(solo_rig::RigPreset preset, std::string id = "rig") {
    solo_rig::PresetPreparationService service;
    const auto prepared = service.Prepare(std::move(id), std::move(preset));
    REQUIRE(std::holds_alternative<solo_rig::PreparedPreset>(prepared));
    return std::get<solo_rig::PreparedPreset>(prepared);
}

solo_rig::RigModuleFactory GainFactory(std::uint32_t latency = 0U) {
    return [latency](const solo_rig::PresetNode& node)
               -> std::variant<std::unique_ptr<solo_rig::ModuleRuntimeProcessor>, solo_rig::RigBuildError> {
        if (node.module_id != "test.gain") {
            return solo_rig::RigBuildError::UnknownModuleId;
        }
        const auto parameter = node.parameter_values.find("gain");
        if (parameter == node.parameter_values.end() || parameter->second < 0.0F || parameter->second > 2.0F) {
            return solo_rig::RigBuildError::ParameterMismatch;
        }
        auto processor = std::make_unique<GainProcessor>(parameter->second);
        processor->SetLatency(latency);
        return std::unique_ptr<solo_rig::ModuleRuntimeProcessor>(std::move(processor));
    };
}

} // namespace

TEST_CASE("RigRuntimeBuilder creates a prepared serial graph and parameters affect audio") {
    solo_rig::RigModuleRegistry registry;
    REQUIRE(!registry.Register("test.gain", GainFactory()).has_value());
    auto prepared = Prepare(MakeGainPreset("Serial", 0.5F));
    const auto built = solo_rig::RigRuntimeBuilder{}.Build(prepared, registry, TestFormat());
    REQUIRE(std::holds_alternative<solo_rig::PreparedPreset>(built));
    REQUIRE(std::get<solo_rig::PreparedPreset>(built).runtime_graph != nullptr);

    solo_rig::RealtimePresetMailbox mailbox;
    auto runtime_prepared = std::get<solo_rig::PreparedPreset>(built);
    mailbox.Submit(runtime_prepared);
    solo_rig::RigRuntime runtime(mailbox, TestFormat());
    REQUIRE(runtime.Prepare());
    solo_rig::AudioBuffer input(1, 64);
    solo_rig::AudioBuffer output(1, 64);
    input.ChannelData(0)[0] = 1.0F;
    REQUIRE(runtime.Process(input, output, 1));
    REQUIRE(output.ChannelData(0)[0] == 0.5F);
    const auto performance = runtime.PerformanceSnapshot();
    REQUIRE(performance.size() == 1U);
    REQUIRE(performance.front().node_id == 2U);
    REQUIRE(performance.front().declared_latency_samples == 0U);
    REQUIRE(std::isfinite(performance.front().last_cpu_percent));
    REQUIRE(std::isfinite(performance.front().peak_cpu_percent));
    REQUIRE(performance.front().peak_cpu_percent >= performance.front().last_cpu_percent);
}

TEST_CASE("RigRuntimeBuilder instantiates a registered external module package and routes its output") {
    solo_rig::RigModuleRegistry registry;
    solo_rig::ModulePackage package{
        .descriptor = {.module_id = "external.gain",
                       .display_name = "External Gain",
                       .api_version = 1,
                       .category = solo_rig::EffectCategory::DigitalAlgorithm,
                       .logic_entry_point = "logic.json",
                       .ports = {{.id = "input", .direction = solo_rig::PortDirection::Input, .max_channels = 1},
                                 {.id = "output", .direction = solo_rig::PortDirection::Output, .max_channels = 1}},
                       .parameters = {{.id = "gain", .minimum = 0.0F, .maximum = 2.0F, .default_value = 0.5F}}},
        .logic = solo_rig::DigitalAlgorithmLogic{
            .algorithm_id = "linear-gain",
            .operations = {{.type = solo_rig::DigitalOperationType::Gain, .parameter_id = "gain"}}},
    };
    REQUIRE(!registry.RegisterPackage(std::move(package)).has_value());
    auto prepared = Prepare({.name = "External",
                             .nodes = {{.instance_id = "external", .module_id = "external.gain", .parameter_values = {{"gain", 0.25F}}}}},
                            "external");
    const auto built = solo_rig::RigRuntimeBuilder{}.Build(prepared, registry, TestFormat());
    REQUIRE(std::holds_alternative<solo_rig::PreparedPreset>(built));

    solo_rig::RealtimePresetMailbox mailbox;
    const auto runtime_prepared = std::get<solo_rig::PreparedPreset>(built);
    mailbox.Submit(runtime_prepared);
    solo_rig::RigRuntime runtime(mailbox, TestFormat());
    REQUIRE(runtime.Prepare());
    solo_rig::AudioBuffer input(1, 64);
    solo_rig::AudioBuffer output(1, 64);
    input.ChannelData(0)[0] = 1.0F;
    REQUIRE(runtime.Process(input, output, 1));
    REQUIRE(output.ChannelData(0)[0] == 0.25F);
}

TEST_CASE("RigRuntimeBuilder supports zero-node passthrough, parallel branches and latency compensation") {
    solo_rig::RigModuleRegistry registry;
    REQUIRE(!registry.Register("test.gain", GainFactory(0U)).has_value());
    auto passthrough = Prepare({.name = "Empty"}, "empty");
    const auto built_empty = solo_rig::RigRuntimeBuilder{}.Build(passthrough, registry, TestFormat());
    REQUIRE(std::holds_alternative<solo_rig::PreparedPreset>(built_empty));
    REQUIRE(std::get<solo_rig::PreparedPreset>(built_empty).runtime_graph == nullptr);

    auto parallel = Prepare({.name = "Parallel",
                             .nodes = {{.instance_id = "short", .module_id = "test.gain", .parameter_values = {{"gain", 1.0F}}},
                                       {.instance_id = "long", .module_id = "test.gain", .parameter_values = {{"gain", 1.0F}}}},
                             .connections = {{.source_instance_id = "short", .destination_instance_id = "long"}},
                            },
                            "parallel");
    const auto built_parallel = solo_rig::RigRuntimeBuilder{}.Build(parallel, registry, TestFormat());
    REQUIRE(std::holds_alternative<solo_rig::PreparedPreset>(built_parallel));
    REQUIRE(std::get<solo_rig::PreparedPreset>(built_parallel).runtime_graph->DeclaredLatencySamples() == 0U);
}

TEST_CASE("RigRuntime rejects unknown modules, bad parameters and invalid topology before mailbox submission") {
    solo_rig::RigModuleRegistry registry;
    REQUIRE(!registry.Register("test.gain", GainFactory()).has_value());
    auto unknown = Prepare({.name = "Unknown",
                            .nodes = {{.instance_id = "x", .module_id = "missing.module"}}},
                           "unknown");
    const auto unknown_result = solo_rig::RigRuntimeBuilder{}.Build(unknown, registry, TestFormat());
    REQUIRE(std::holds_alternative<solo_rig::RigBuildError>(unknown_result));
    REQUIRE(std::get<solo_rig::RigBuildError>(unknown_result) == solo_rig::RigBuildError::UnknownModuleId);

    auto bad_parameter = Prepare({.name = "Bad Parameter",
                                  .nodes = {{.instance_id = "x", .module_id = "test.gain", .parameter_values = {{"wrong", 1.0F}}}}},
                                 "bad-parameter");
    const auto bad_result = solo_rig::RigRuntimeBuilder{}.Build(bad_parameter, registry, TestFormat());
    REQUIRE(std::holds_alternative<solo_rig::RigBuildError>(bad_result));
    REQUIRE(std::get<solo_rig::RigBuildError>(bad_result) == solo_rig::RigBuildError::ParameterMismatch);

    auto invalid = Prepare({.name = "Cycle",
                            .nodes = {{.instance_id = "a", .module_id = "test.gain", .parameter_values = {{"gain", 1.0F}}},
                                      {.instance_id = "b", .module_id = "test.gain", .parameter_values = {{"gain", 1.0F}}}},
                            .connections = {{.source_instance_id = "a", .destination_instance_id = "b"},
                                            {.source_instance_id = "b", .destination_instance_id = "a"}}},
                           "cycle");
    const auto invalid_result = solo_rig::RigRuntimeBuilder{}.Build(invalid, registry, TestFormat());
    REQUIRE(std::holds_alternative<solo_rig::RigBuildError>(invalid_result));
    REQUIRE(std::get<solo_rig::RigBuildError>(invalid_result) == solo_rig::RigBuildError::GraphPreparationFailed);
}

TEST_CASE("RigRuntime performs sample-level equal-power transition and commits only at the end") {
    solo_rig::RigModuleRegistry registry;
    REQUIRE(!registry.Register("test.gain", GainFactory()).has_value());
    auto first = Prepare(MakeGainPreset("First", 1.0F), "first");
    auto second = Prepare(MakeGainPreset("Second", 0.0F), "second");
    first = std::get<solo_rig::PreparedPreset>(solo_rig::RigRuntimeBuilder{}.Build(first, registry, TestFormat()));
    second = std::get<solo_rig::PreparedPreset>(solo_rig::RigRuntimeBuilder{}.Build(second, registry, TestFormat()));
    solo_rig::RealtimePresetMailbox mailbox;
    mailbox.Submit(first);
    solo_rig::RigRuntime runtime(mailbox, TestFormat(), {.crossfade_milliseconds = 1.0F});
    REQUIRE(runtime.Prepare());
    solo_rig::AudioBuffer input(1, 64);
    solo_rig::AudioBuffer output(1, 64);
    input.ChannelData(0)[0] = 1.0F;
    REQUIRE(runtime.Process(input, output, 1));
    mailbox.Submit(second);
    input.Clear(64);
    input.ChannelData(0)[0] = 1.0F;
    REQUIRE(runtime.Process(input, output, 47));
    REQUIRE(runtime.InTransition());
    REQUIRE(output.ChannelData(0)[0] == 1.0F);
    REQUIRE(output.ChannelData(0)[46] < 0.1F);
    REQUIRE(output.ChannelData(0)[46] >= 0.0F);
    REQUIRE(runtime.Process(input, output, 1));
    REQUIRE(!runtime.InTransition());
    REQUIRE(output.ChannelData(0)[0] == 0.0F);
    REQUIRE(mailbox.Active()->preset_id == "second");
}

TEST_CASE("PresetController uses bounded prepared slots and reports pressure instead of unbounded growth") {
    solo_rig::RigModuleRegistry registry;
    REQUIRE(!registry.Register("test.gain", GainFactory()).has_value());
    solo_rig::PresetController controller(&registry, TestFormat());
    REQUIRE(controller.SubmittedSlotCapacity() == 8U);
    REQUIRE(!controller.RegisterPreset("one", MakeGainPreset("One", 1.0F)).has_value());
    solo_rig::RigRuntime runtime(controller.MailboxForControl(), TestFormat(), {.crossfade_milliseconds = 1.0F});
    REQUIRE(runtime.Prepare());
    solo_rig::AudioBuffer input(1, 64);
    solo_rig::AudioBuffer output(1, 64);
    input.ChannelData(0)[0] = 1.0F;
    REQUIRE(!controller.QueuePresetActivation("one").has_value());
    REQUIRE(runtime.Process(input, output, 1));
    REQUIRE(!controller.QueuePresetActivation("one").has_value());
    REQUIRE(controller.QueuePresetActivation("one").value() == solo_rig::PresetControlError::RuntimeStorageExhausted);
    REQUIRE(runtime.Process(input, output, 48));
    REQUIRE(!runtime.InTransition());
    REQUIRE(!controller.QueuePresetActivation("one").has_value());
}

TEST_CASE("Prepared slot ownership survives pending-taken interleaving without overwriting the audio-held object") {
    solo_rig::PresetController controller;
    REQUIRE(!controller.RegisterPreset("one", MakeGainPreset("One", 1.0F)).has_value());
    REQUIRE(!controller.QueuePresetActivation("one").has_value());
    const auto* audio_claimed = controller.MailboxForControl().BeginPendingAtControlPoint();
    REQUIRE(audio_claimed != nullptr);
    REQUIRE(audio_claimed->preset_id == "one");

    REQUIRE(!controller.QueuePresetActivation("one").has_value());
    const auto* queued_replacement = controller.MailboxForControl().Pending();
    REQUIRE(queued_replacement != nullptr);
    REQUIRE(queued_replacement != audio_claimed);
    REQUIRE(controller.QueuePresetActivation("one").value() == solo_rig::PresetControlError::RuntimeStorageExhausted);
    REQUIRE(audio_claimed->preset_id == "one");
    REQUIRE(controller.MailboxForControl().BeginPendingAtControlPoint() == queued_replacement);
    REQUIRE(audio_claimed->preset_id == "one");
}

TEST_CASE("RigRuntime keeps a pending replacement until the current transition commits") {
    solo_rig::RigModuleRegistry registry;
    REQUIRE(!registry.Register("test.gain", GainFactory()).has_value());
    solo_rig::PresetController controller(&registry, TestFormat());
    REQUIRE(!controller.RegisterPreset("one", MakeGainPreset("One", 1.0F)).has_value());
    solo_rig::RigRuntime runtime(controller.MailboxForControl(), TestFormat(), {.crossfade_milliseconds = 1.0F});
    REQUIRE(runtime.Prepare());
    solo_rig::AudioBuffer input(1, 64);
    solo_rig::AudioBuffer output(1, 64);
    input.ChannelData(0)[0] = 1.0F;
    REQUIRE(!controller.QueuePresetActivation("one").has_value());
    REQUIRE(runtime.Process(input, output, 1));
    REQUIRE(!controller.QueuePresetActivation("one").has_value());
    REQUIRE(runtime.Process(input, output, 1));
    REQUIRE(runtime.InTransition());
    REQUIRE(!controller.QueuePresetActivation("one").has_value());
    REQUIRE(controller.MailboxForControl().Pending() != nullptr);
    REQUIRE(runtime.Process(input, output, 47));
    REQUIRE(!runtime.InTransition());
    REQUIRE(controller.MailboxForControl().Pending() != nullptr);
    REQUIRE(runtime.Process(input, output, 1));
    REQUIRE(runtime.InTransition());
}

TEST_CASE("RigRuntime retires a claimed candidate with no graph and preserves the active graph") {
    solo_rig::RigModuleRegistry registry;
    REQUIRE(!registry.Register("test.gain", GainFactory()).has_value());
    solo_rig::PresetController controller(&registry, TestFormat());
    REQUIRE(!controller.RegisterPreset("one", MakeGainPreset("One", 1.0F)).has_value());
    solo_rig::RigRuntime runtime(controller.MailboxForControl(), TestFormat());
    REQUIRE(runtime.Prepare());
    solo_rig::AudioBuffer input(1, 64);
    solo_rig::AudioBuffer output(1, 64);
    input.ChannelData(0)[0] = 1.0F;
    REQUIRE(!controller.QueuePresetActivation("one").has_value());
    REQUIRE(runtime.Process(input, output, 1));
    const auto* active_before = controller.ActivePreset();

    std::atomic<solo_rig::PreparedSlotState> invalid_state{solo_rig::PreparedSlotState::Published};
    solo_rig::PreparedPreset invalid{.preset_id = "invalid",
                                     .preset = {.preset_id = "invalid",
                                                .name = "Invalid",
                                                .nodes = {{.instance_id = "node", .module_id = "missing"}}},
                                     .runtime_graph = nullptr,
                                     .slot_state = &invalid_state};
    REQUIRE(controller.MailboxForControl().Publish(invalid));
    REQUIRE(runtime.Process(input, output, 1));
    REQUIRE(invalid_state.load(std::memory_order_acquire) == solo_rig::PreparedSlotState::Retired);
    REQUIRE(controller.ActivePreset() == active_before);
}

TEST_CASE("RigRuntime reuses retired slots across one thousand completed transitions") {
    solo_rig::RigModuleRegistry registry;
    REQUIRE(!registry.Register("test.gain", GainFactory()).has_value());
    solo_rig::PresetController controller(&registry, TestFormat());
    REQUIRE(!controller.RegisterPreset("one", MakeGainPreset("One", 1.0F)).has_value());
    solo_rig::RigRuntime runtime(controller.MailboxForControl(), TestFormat(), {.crossfade_milliseconds = 1.0F});
    REQUIRE(runtime.Prepare());
    solo_rig::AudioBuffer input(1, 64);
    solo_rig::AudioBuffer output(1, 64);
    input.ChannelData(0)[0] = 1.0F;
    REQUIRE(!controller.QueuePresetActivation("one").has_value());
    REQUIRE(runtime.Process(input, output, 1));
    for (std::size_t cycle = 0; cycle < 1000U; ++cycle) {
        REQUIRE(!controller.QueuePresetActivation("one").has_value());
        REQUIRE(runtime.Process(input, output, 48));
        REQUIRE(!runtime.InTransition());
        REQUIRE(controller.ActivePreset() != nullptr);
    }
}

TEST_CASE("RigRuntime shutdown retires pending and active slots for repeated reconfiguration") {
    solo_rig::RigModuleRegistry registry;
    REQUIRE(!registry.Register("test.gain", GainFactory()).has_value());
    solo_rig::PresetController controller(&registry, TestFormat());
    REQUIRE(!controller.RegisterPreset("one", MakeGainPreset("One", 1.0F)).has_value());
    solo_rig::RigRuntime runtime(controller.MailboxForControl(), TestFormat());
    REQUIRE(runtime.Prepare());
    solo_rig::AudioBuffer input(1, 64);
    solo_rig::AudioBuffer output(1, 64);
    input.ChannelData(0)[0] = 1.0F;

    for (std::size_t cycle = 0; cycle < 32U; ++cycle) {
        REQUIRE(!controller.QueuePresetActivation("one").has_value());
        REQUIRE(runtime.Process(input, output, 1));
        REQUIRE(controller.MailboxForControl().Active() != nullptr);
        // A pending slot is intentionally left unconsumed on alternating
        // iterations; Shutdown must retire both ownership paths.
        if ((cycle % 2U) == 0U) {
            REQUIRE(!controller.QueuePresetActivation("one").has_value());
        }
        runtime.Shutdown();
        REQUIRE(controller.MailboxForControl().Active() == nullptr);
        REQUIRE(controller.MailboxForControl().Pending() == nullptr);
        REQUIRE(runtime.Prepare());
    }
}
