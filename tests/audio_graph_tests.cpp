#include "core/audio_graph.hpp"
#include "test_framework.hpp"

#include <memory>

namespace {

class GainProcessor final : public solo_rig::EffectProcessor {
public:
    GainProcessor(float gain, std::uint32_t latency_samples) : gain_(gain), latency_samples_(latency_samples) {}

    bool Prepare(const solo_rig::AudioFormat&) override {
        return true;
    }

    void Process(const solo_rig::AudioBuffer& input,
                 solo_rig::AudioBuffer& output,
                 std::uint32_t sample_count) noexcept override {
        for (std::uint32_t channel = 0; channel < input.ChannelCount(); ++channel) {
            for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
                output.ChannelData(channel)[sample] = input.ChannelData(channel)[sample] * gain_;
            }
        }
    }

    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override {
        return latency_samples_;
    }

private:
    float gain_;
    std::uint32_t latency_samples_;
};

solo_rig::AudioFormat CreateFormat() {
    return {
        .sample_rate_hz = 48000.0,
        .max_block_size = 8,
        .channel_count = 1,
        .sample_representation = solo_rig::SampleRepresentation::Float32,
    };
}

} // namespace

TEST_CASE("AudioGraph processes a serial effect chain and reports cumulative latency") {
    solo_rig::AudioGraph graph;
    const auto first = graph.AddNode(std::make_unique<GainProcessor>(2.0F, 3));
    const auto second = graph.AddNode(std::make_unique<GainProcessor>(3.0F, 5));

    REQUIRE(graph.Connect(solo_rig::AudioGraph::InputNodeId, first));
    REQUIRE(graph.Connect(first, second));
    REQUIRE(graph.Connect(second, solo_rig::AudioGraph::OutputNodeId));
    REQUIRE(graph.Prepare(CreateFormat()).empty());
    REQUIRE(graph.DeclaredLatencySamples() == 8);

    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    input.ChannelData(0)[0] = 0.5F;

    REQUIRE(graph.Process(input, output, 1));
    REQUIRE(output.ChannelData(0)[0] == 3.0F);
}

TEST_CASE("AudioGraph sums parallel branches and reports the longest latency path") {
    solo_rig::AudioGraph graph;
    const auto left = graph.AddNode(std::make_unique<GainProcessor>(2.0F, 3));
    const auto right = graph.AddNode(std::make_unique<GainProcessor>(3.0F, 9));

    REQUIRE(graph.Connect(solo_rig::AudioGraph::InputNodeId, left));
    REQUIRE(graph.Connect(solo_rig::AudioGraph::InputNodeId, right));
    REQUIRE(graph.Connect(left, solo_rig::AudioGraph::OutputNodeId));
    REQUIRE(graph.Connect(right, solo_rig::AudioGraph::OutputNodeId));
    REQUIRE(graph.Prepare(CreateFormat()).empty());
    REQUIRE(graph.DeclaredLatencySamples() == 9);

    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    input.ChannelData(0)[0] = 1.0F;

    REQUIRE(graph.Process(input, output, 1));
    REQUIRE(output.ChannelData(0)[0] == 5.0F);
}

TEST_CASE("AudioGraph rejects cyclic signal routes before real-time processing") {
    solo_rig::AudioGraph graph;
    const auto first = graph.AddNode(std::make_unique<GainProcessor>(1.0F, 0));
    const auto second = graph.AddNode(std::make_unique<GainProcessor>(1.0F, 0));

    REQUIRE(graph.Connect(solo_rig::AudioGraph::InputNodeId, first));
    REQUIRE(graph.Connect(first, second));
    REQUIRE(graph.Connect(second, first));
    REQUIRE(graph.Connect(second, solo_rig::AudioGraph::OutputNodeId));

    const auto errors = graph.Prepare(CreateFormat());
    REQUIRE(!errors.empty());
    REQUIRE(errors.front() == solo_rig::GraphPreparationError::CycleDetected);
}

TEST_CASE("AudioGraph rejects processing blocks larger than its preallocated capacity") {
    solo_rig::AudioGraph graph;
    const auto processor = graph.AddNode(std::make_unique<GainProcessor>(1.0F, 0));
    REQUIRE(graph.Connect(solo_rig::AudioGraph::InputNodeId, processor));
    REQUIRE(graph.Connect(processor, solo_rig::AudioGraph::OutputNodeId));
    REQUIRE(graph.Prepare(CreateFormat()).empty());

    solo_rig::AudioBuffer input(1, 9);
    solo_rig::AudioBuffer output(1, 9);
    REQUIRE(!graph.Process(input, output, 9));
}
