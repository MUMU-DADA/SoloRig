#pragma once

#include "core/audio_buffer.hpp"
#include "core/audio_format.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace solo_rig {

class EffectProcessor {
public:
    virtual ~EffectProcessor() = default;

    virtual bool Prepare(const AudioFormat& format) = 0;
    virtual void Process(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept = 0;
    [[nodiscard]] virtual std::uint32_t DeclaredLatencySamples() const noexcept = 0;
};

enum class GraphPreparationError {
    InvalidAudioFormat,
    MissingProcessor,
    InvalidConnection,
    CycleDetected,
    ProcessorRejectedFormat,
    MissingOutputRoute,
    LatencyOverflow,
    LatencyCompensationAllocationFailed,
};

class AudioGraph {
public:
    using NodeId = std::uint32_t;

    static constexpr NodeId InputNodeId = 0;
    static constexpr NodeId OutputNodeId = 1;

    [[nodiscard]] NodeId AddNode(std::unique_ptr<EffectProcessor> processor);
    [[nodiscard]] bool Connect(NodeId source, NodeId destination);
    [[nodiscard]] std::vector<GraphPreparationError> Prepare(const AudioFormat& format);
    [[nodiscard]] bool Process(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept;
    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept;

private:
    struct DelayCompensator {
        std::uint32_t delay_samples{};
        std::uint32_t write_position{};
        std::vector<float> delay_line;

        [[nodiscard]] bool Prepare(std::uint32_t channel_count);
        void AddFrom(const AudioBuffer& source, AudioBuffer& destination, std::uint32_t sample_count) noexcept;
    };

    struct Route {
        NodeId source{};
        DelayCompensator compensation;
    };

    struct Node {
        NodeId id{};
        std::unique_ptr<EffectProcessor> processor;
        std::vector<NodeId> incoming;
        std::vector<NodeId> outgoing;
        std::vector<Route> input_routes;
        AudioBuffer input_buffer;
        AudioBuffer output_buffer;
        std::uint32_t input_latency_samples{};
        std::uint32_t cumulative_latency_samples{};
    };

    [[nodiscard]] Node* FindNode(NodeId id) noexcept;
    [[nodiscard]] const Node* FindNode(NodeId id) const noexcept;
    [[nodiscard]] bool IsKnownNode(NodeId id) const noexcept;

    std::vector<Node> nodes_;
    std::vector<NodeId> process_order_;
    std::vector<NodeId> output_incoming_;
    std::vector<Route> output_routes_;
    AudioFormat format_;
    bool prepared_{};
    std::uint32_t declared_latency_samples_{};
};

} // namespace solo_rig
