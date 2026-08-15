#include "content/behavior_audio_processor.hpp"
#include "content/behavior_ports.hpp"
#include "core/named_audio_graph.hpp"
#include "test_framework.hpp"

#include "../fixtures/whitebox/m19c_representative_pedal_fixtures.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

class NamedGainProcessor final : public solo_rig::NamedAudioPortProcessor {
public:
    explicit NamedGainProcessor(float gain) : gain_(gain) {}

    [[nodiscard]] solo_rig::NamedAudioPortContractList InputPorts() const noexcept override {
        return {.ports = {{{"in", solo_rig::NamedAudioPortDirection::Input,
                             solo_rig::NamedAudioPortDomain::AudioSignal, 0U}}},
                .count = 1U};
    }

    [[nodiscard]] solo_rig::NamedAudioPortContractList OutputPorts() const noexcept override {
        return {.ports = {{{"out", solo_rig::NamedAudioPortDirection::Output,
                             solo_rig::NamedAudioPortDomain::AudioSignal, 0U}}},
                .count = 1U};
    }

    [[nodiscard]] bool Prepare(const solo_rig::AudioFormat& format,
                               std::uint32_t) override {
        prepared_ = !format.Validate().empty() ? false : true;
        return prepared_;
    }

    [[nodiscard]] bool Process(std::span<const solo_rig::NamedAudioPortInput> inputs,
                               std::span<solo_rig::NamedAudioPortOutput> outputs,
                               std::uint32_t sample_count) noexcept override {
        if (!prepared_ || inputs.size() != 1U || outputs.size() != 1U ||
            inputs[0].buffer == nullptr || outputs[0].buffer == nullptr) return false;
        for (std::uint32_t channel = 0U; channel < inputs[0].buffer->ChannelCount(); ++channel) {
            for (std::uint32_t sample = 0U; sample < sample_count; ++sample) {
                outputs[0].buffer->ChannelData(channel)[sample] =
                    inputs[0].buffer->ChannelData(channel)[sample] * gain_;
            }
        }
        return true;
    }

    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override { return 0U; }
    void Reset() noexcept override {}

private:
    float gain_{};
    bool prepared_{};
};

solo_rig::AudioFormat Format() {
    return {.sample_rate_hz = 192000.0,
            .max_block_size = 8U,
            .channel_count = 1U,
            .sample_representation = solo_rig::SampleRepresentation::Float32};
}

solo_rig::BehaviorGraphNodeSpec Node(std::string id, std::string kind, std::string model) {
    return {.id = id,
            .binding = {.module_id = id, .kind = std::move(kind), .model_id = std::move(model)}};
}

solo_rig::BehaviorTypedGraphPlan EnvelopeVcaPlan() {
    const auto nodes = std::vector<solo_rig::BehaviorGraphNodeSpec>{
        Node("envelope", "envelope", "generic-envelope-v1"),
        Node("vca", "vca", "generic-vca-v1")};
    const auto routes = std::vector<solo_rig::BehaviorPortRouteSpec>{
        {"input", "audio", "vca", "audio_in"},
        {"input", "detector", "envelope", "detector"},
        {"envelope", "out", "vca", "control"},
        {"vca", "out", "output", "audio"}};
    const auto compiled = solo_rig::CompileTypedBehaviorGraph(nodes, routes);
    REQUIRE(compiled.plan.has_value());
    return *compiled.plan;
}

solo_rig::BehaviorTypedGraphPlan EnvelopePlan() {
    const auto nodes = std::vector<solo_rig::BehaviorGraphNodeSpec>{
        Node("envelope", "envelope", "generic-envelope-v1")};
    const auto routes = std::vector<solo_rig::BehaviorPortRouteSpec>{
        {"input", "detector", "envelope", "detector"},
        {"envelope", "out", "output", "control"}};
    const auto compiled = solo_rig::CompileTypedBehaviorGraph(nodes, routes);
    REQUIRE(compiled.plan.has_value());
    return *compiled.plan;
}

solo_rig::MnaPreparedCircuit PreparedTone() {
    auto circuit = solo_rig::m19c_fixtures::PassiveTone(4U, 0.5);
    circuit.parameter_bindings = {{.model_version = 1U,
        .parameter_id = "tone", .target_id = "tone_pot",
        .property = solo_rig::CircuitParameterBindingProperty::PotentiometerPosition,
        .smoothing_seconds = 0.0}};
    const auto compiled = solo_rig::CompileMnaCircuit(circuit);
    REQUIRE(compiled.circuit.has_value());
    return std::move(*compiled.circuit);
}

void AddAudioBoundary(solo_rig::NamedPortAudioGraph& graph) {
    REQUIRE(graph.AddInputPort("audio", solo_rig::NamedAudioPortDomain::AudioSignal, 1U));
    REQUIRE(graph.AddOutputPort("audio", solo_rig::NamedAudioPortDomain::AudioSignal, 1U));
}

} // namespace

TEST_CASE("M22-B2 named port graph executes typed detector sidechain at 192 kHz") {
    solo_rig::NamedPortAudioGraph graph;
    REQUIRE(graph.AddInputPort("audio", solo_rig::NamedAudioPortDomain::AudioSignal, 1U));
    REQUIRE(graph.AddInputPort("detector", solo_rig::NamedAudioPortDomain::DetectorInput, 1U));
    REQUIRE(graph.AddOutputPort("audio", solo_rig::NamedAudioPortDomain::AudioSignal, 1U));
    REQUIRE(graph.AddNode("behavior", std::make_unique<solo_rig::TypedBehaviorPortProcessor>(
        EnvelopeVcaPlan(), 4U)));
    REQUIRE(graph.Connect({"input", "audio", "behavior", "audio"}));
    REQUIRE(graph.Connect({"input", "detector", "behavior", "detector"}));
    REQUIRE(graph.Connect({"behavior", "audio", "output", "audio"}));
    REQUIRE(graph.Prepare(Format(), 4U).empty());
    REQUIRE(graph.InputPortCount() == 2U);
    REQUIRE(graph.OutputPortCount() == 1U);

    std::array<solo_rig::AudioBuffer, 2U> inputs{
        solo_rig::AudioBuffer(1U, 8U), solo_rig::AudioBuffer(1U, 8U)};
    std::array<solo_rig::AudioBuffer, 1U> outputs{solo_rig::AudioBuffer(1U, 8U)};
    inputs[0].ChannelData(0U)[0] = 0.5F;
    inputs[1].ChannelData(0U)[0] = 0.75F;
    REQUIRE(graph.Process(std::span<const solo_rig::AudioBuffer>(inputs),
                          std::span<solo_rig::AudioBuffer>(outputs), 8U));
    REQUIRE(std::isfinite(outputs[0].ChannelData(0U)[0]));
}

TEST_CASE("M22-B2 named port graph accepts only explicitly delayed feedback cycles") {
    solo_rig::NamedPortAudioGraph graph;
    AddAudioBoundary(graph);
    REQUIRE(graph.AddNode("forward", std::make_unique<NamedGainProcessor>(1.0F)));
    REQUIRE(graph.AddNode("feedback", std::make_unique<NamedGainProcessor>(1.0F)));
    REQUIRE(graph.Connect({"input", "audio", "forward", "in"}));
    REQUIRE(graph.Connect({"forward", "out", "feedback", "in"}));
    REQUIRE(graph.Connect({"feedback", "out", "forward", "in", true}));
    REQUIRE(graph.Connect({"feedback", "out", "output", "audio"}));
    REQUIRE(graph.Prepare(Format()).empty());

    std::array<solo_rig::AudioBuffer, 1U> inputs{solo_rig::AudioBuffer(1U, 8U)};
    std::array<solo_rig::AudioBuffer, 1U> outputs{solo_rig::AudioBuffer(1U, 8U)};
    inputs[0].ChannelData(0U)[0] = 1.0F;
    REQUIRE(graph.Process(std::span<const solo_rig::AudioBuffer>(inputs),
                          std::span<solo_rig::AudioBuffer>(outputs), 1U));
    REQUIRE(outputs[0].ChannelData(0U)[0] == 1.0F);
    inputs[0].ChannelData(0U)[0] = 1.0F;
    REQUIRE(graph.Process(std::span<const solo_rig::AudioBuffer>(inputs),
                          std::span<solo_rig::AudioBuffer>(outputs), 1U));
    REQUIRE(outputs[0].ChannelData(0U)[0] == 2.0F);

    solo_rig::NamedPortAudioGraph zero_delay;
    AddAudioBoundary(zero_delay);
    REQUIRE(zero_delay.AddNode("forward", std::make_unique<NamedGainProcessor>(1.0F)));
    REQUIRE(zero_delay.AddNode("feedback", std::make_unique<NamedGainProcessor>(1.0F)));
    REQUIRE(zero_delay.Connect({"input", "audio", "forward", "in"}));
    REQUIRE(zero_delay.Connect({"forward", "out", "feedback", "in"}));
    REQUIRE(zero_delay.Connect({"feedback", "out", "forward", "in"}));
    REQUIRE(zero_delay.Connect({"feedback", "out", "output", "audio"}));
    const auto errors = zero_delay.Prepare(Format());
    REQUIRE(!errors.empty());
    REQUIRE(errors.front() == solo_rig::NamedAudioGraphPreparationError::CycleDetected);
}

TEST_CASE("M22-B2 named port graph consumes typed control in an MNA composite") {
    solo_rig::NamedPortAudioGraph graph;
    REQUIRE(graph.AddInputPort("audio", solo_rig::NamedAudioPortDomain::AudioSignal, 1U));
    REQUIRE(graph.AddInputPort("detector", solo_rig::NamedAudioPortDomain::DetectorInput, 1U));
    REQUIRE(graph.AddOutputPort("audio", solo_rig::NamedAudioPortDomain::AudioSignal, 1U));
    const solo_rig::TypedBehaviorMnaBinding binding{
        .control_index = 0U,
        .source_domain = solo_rig::BehaviorPortDomain::ControlSignal,
        .source_minimum = 0.0,
        .source_maximum = 1.0,
        .target_minimum = 0.0,
        .target_maximum = 1.0};
    REQUIRE(graph.AddNode("whitebox", std::make_unique<solo_rig::TypedBehaviorMnaPortProcessor>(
        EnvelopePlan(), PreparedTone(), std::span<const solo_rig::TypedBehaviorMnaBinding>(&binding, 1U), 4U)));
    REQUIRE(graph.Connect({"input", "audio", "whitebox", "audio"}));
    REQUIRE(graph.Connect({"input", "detector", "whitebox", "detector"}));
    REQUIRE(graph.Connect({"whitebox", "audio", "output", "audio"}));
    REQUIRE(graph.Prepare(Format(), 4U).empty());

    std::array<solo_rig::AudioBuffer, 2U> inputs{
        solo_rig::AudioBuffer(1U, 8U), solo_rig::AudioBuffer(1U, 8U)};
    std::array<solo_rig::AudioBuffer, 1U> outputs{solo_rig::AudioBuffer(1U, 8U)};
    inputs[0].ChannelData(0U)[0] = 0.25F;
    inputs[1].ChannelData(0U)[0] = 0.75F;
    REQUIRE(graph.Process(std::span<const solo_rig::AudioBuffer>(inputs),
                          std::span<solo_rig::AudioBuffer>(outputs), 1U));
    REQUIRE(std::isfinite(outputs[0].ChannelData(0U)[0]));
}

TEST_CASE("M22-B2 named port graph rejects scalar typed behavior on multichannel audio") {
    solo_rig::NamedPortAudioGraph graph;
    AddAudioBoundary(graph);
    REQUIRE(graph.AddNode("behavior", std::make_unique<solo_rig::TypedBehaviorPortProcessor>(
        EnvelopeVcaPlan(), 1U)));
    REQUIRE(graph.Connect({"input", "audio", "behavior", "audio"}));
    REQUIRE(graph.Connect({"behavior", "audio", "output", "audio"}));
    const auto errors = graph.Prepare({.sample_rate_hz = 192000.0,
                                       .max_block_size = 8U,
                                       .channel_count = 2U}, 1U);
    REQUIRE(!errors.empty());
    REQUIRE(errors.front() == solo_rig::NamedAudioGraphPreparationError::ProcessorRejectedFormat);
}
