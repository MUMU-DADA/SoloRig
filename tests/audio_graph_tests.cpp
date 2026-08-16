#include "core/audio_graph.hpp"
#include "test_framework.hpp"

#include <memory>
#include <vector>

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

class DelayedGainProcessor final : public solo_rig::EffectProcessor {
public:
    DelayedGainProcessor(float gain, std::uint32_t latency_samples)
        : gain_(gain), latency_samples_(latency_samples) {}

    bool Prepare(const solo_rig::AudioFormat& format) override {
        channel_count_ = format.channel_count;
        delay_line_.assign(static_cast<std::size_t>(channel_count_) * latency_samples_, 0.0F);
        write_position_ = 0;
        return true;
    }

    void Process(const solo_rig::AudioBuffer& input,
                 solo_rig::AudioBuffer& output,
                 std::uint32_t sample_count) noexcept override {
        if (latency_samples_ == 0) {
            for (std::uint32_t channel = 0; channel < input.ChannelCount(); ++channel) {
                for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
                    output.ChannelData(channel)[sample] = input.ChannelData(channel)[sample] * gain_;
                }
            }
            return;
        }

        for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
            for (std::uint32_t channel = 0; channel < channel_count_; ++channel) {
                const auto index = static_cast<std::size_t>(channel) * latency_samples_ + write_position_;
                const auto delayed_sample = delay_line_[index];
                delay_line_[index] = input.ChannelData(channel)[sample];
                output.ChannelData(channel)[sample] = delayed_sample * gain_;
            }
            write_position_ = (write_position_ + 1U) % latency_samples_;
        }
    }

    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override {
        return latency_samples_;
    }

private:
    float gain_;
    std::uint32_t latency_samples_;
    std::uint32_t channel_count_{};
    std::uint32_t write_position_{};
    std::vector<float> delay_line_;
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

TEST_CASE("AudioGraph bypass copies the node input while preserving graph routing") {
    solo_rig::AudioGraph graph;
    const auto node = graph.AddNode(std::make_unique<GainProcessor>(2.0F, 0), true);
    REQUIRE(graph.Connect(solo_rig::AudioGraph::InputNodeId, node));
    REQUIRE(graph.Connect(node, solo_rig::AudioGraph::OutputNodeId));
    REQUIRE(graph.Prepare(CreateFormat()).empty());

    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    input.ChannelData(0)[0] = 0.5F;
    REQUIRE(graph.Process(input, output, 1));
    REQUIRE(output.ChannelData(0)[0] == 0.5F);
}

TEST_CASE("AudioGraph sums zero-latency parallel branches") {
    solo_rig::AudioGraph graph;
    const auto left = graph.AddNode(std::make_unique<GainProcessor>(2.0F, 0));
    const auto right = graph.AddNode(std::make_unique<GainProcessor>(3.0F, 0));

    REQUIRE(graph.Connect(solo_rig::AudioGraph::InputNodeId, left));
    REQUIRE(graph.Connect(solo_rig::AudioGraph::InputNodeId, right));
    REQUIRE(graph.Connect(left, solo_rig::AudioGraph::OutputNodeId));
    REQUIRE(graph.Connect(right, solo_rig::AudioGraph::OutputNodeId));
    REQUIRE(graph.Prepare(CreateFormat()).empty());
    REQUIRE(graph.DeclaredLatencySamples() == 0);

    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    input.ChannelData(0)[0] = 1.0F;

    REQUIRE(graph.Process(input, output, 1));
    REQUIRE(output.ChannelData(0)[0] == 5.0F);
}

TEST_CASE("AudioGraph delay-compensates parallel branches before mixing") {
    solo_rig::AudioGraph graph;
    const auto short_path = graph.AddNode(std::make_unique<DelayedGainProcessor>(2.0F, 3));
    const auto long_path = graph.AddNode(std::make_unique<DelayedGainProcessor>(3.0F, 9));

    REQUIRE(graph.Connect(solo_rig::AudioGraph::InputNodeId, short_path));
    REQUIRE(graph.Connect(solo_rig::AudioGraph::InputNodeId, long_path));
    REQUIRE(graph.Connect(short_path, solo_rig::AudioGraph::OutputNodeId));
    REQUIRE(graph.Connect(long_path, solo_rig::AudioGraph::OutputNodeId));
    REQUIRE(graph.Prepare(CreateFormat()).empty());
    REQUIRE(graph.DeclaredLatencySamples() == 9U);

    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    input.ChannelData(0)[0] = 1.0F;

    REQUIRE(graph.Process(input, output, 8));
    REQUIRE(output.ChannelData(0)[0] == 0.0F);

    input.Clear(8);
    REQUIRE(graph.Process(input, output, 8));
    REQUIRE(output.ChannelData(0)[0] == 0.0F);
    REQUIRE(output.ChannelData(0)[1] == 5.0F);
}

TEST_CASE("AudioGraph delay-compensates branches that merge into a downstream processor") {
    solo_rig::AudioGraph graph;
    const auto short_path = graph.AddNode(std::make_unique<DelayedGainProcessor>(2.0F, 1));
    const auto long_path = graph.AddNode(std::make_unique<DelayedGainProcessor>(3.0F, 3));
    const auto merge = graph.AddNode(std::make_unique<GainProcessor>(1.0F, 0));

    REQUIRE(graph.Connect(solo_rig::AudioGraph::InputNodeId, short_path));
    REQUIRE(graph.Connect(solo_rig::AudioGraph::InputNodeId, long_path));
    REQUIRE(graph.Connect(short_path, merge));
    REQUIRE(graph.Connect(long_path, merge));
    REQUIRE(graph.Connect(merge, solo_rig::AudioGraph::OutputNodeId));
    REQUIRE(graph.Prepare(CreateFormat()).empty());
    REQUIRE(graph.DeclaredLatencySamples() == 3U);

    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    input.ChannelData(0)[0] = 1.0F;

    REQUIRE(graph.Process(input, output, 8));
    REQUIRE(output.ChannelData(0)[0] == 0.0F);
    REQUIRE(output.ChannelData(0)[3] == 5.0F);
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

TEST_CASE("AudioGraph records per-node measured block load and declared latency") {
    solo_rig::AudioGraph graph;
    const auto node = graph.AddNode(std::make_unique<GainProcessor>(1.0F, 7U));
    REQUIRE(graph.Connect(solo_rig::AudioGraph::InputNodeId, node));
    REQUIRE(graph.Connect(node, solo_rig::AudioGraph::OutputNodeId));
    const solo_rig::AudioFormat format{.sample_rate_hz = 192000.0, .max_block_size = 8, .channel_count = 1};
    REQUIRE(graph.Prepare(format).empty());
    solo_rig::AudioBuffer input(1, 8);
    solo_rig::AudioBuffer output(1, 8);
    REQUIRE(graph.Process(input, output, 8));

    const auto performance = graph.PerformanceSnapshot();
    REQUIRE(performance.size() == 1U);
    REQUIRE(performance[0].node_id == node);
    REQUIRE(performance[0].declared_latency_samples == 7U);
    REQUIRE(std::isfinite(performance[0].last_cpu_percent));
    REQUIRE(std::isfinite(performance[0].peak_cpu_percent));
    REQUIRE(performance[0].last_cpu_percent >= 0.0);
    REQUIRE(performance[0].peak_cpu_percent >= performance[0].last_cpu_percent);
}
