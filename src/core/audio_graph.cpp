#include "core/audio_graph.hpp"

#include <algorithm>
#include <deque>
#include <limits>

namespace solo_rig {

AudioGraph::NodeId AudioGraph::AddNode(std::unique_ptr<EffectProcessor> processor) {
    const auto next_id = static_cast<NodeId>(nodes_.size()) + 2U;
    nodes_.push_back({.id = next_id, .processor = std::move(processor)});
    prepared_ = false;
    return next_id;
}

bool AudioGraph::Connect(NodeId source, NodeId destination) {
    if (!IsKnownNode(source) || !IsKnownNode(destination) || source == OutputNodeId || destination == InputNodeId ||
        source == destination) {
        return false;
    }

    auto* source_node = FindNode(source);
    auto* destination_node = FindNode(destination);
    const auto& existing_destinations = source == InputNodeId ?
                                            std::vector<NodeId>{} : source_node->outgoing;
    if ((source == InputNodeId && destination_node != nullptr &&
         std::ranges::find(destination_node->incoming, source) != destination_node->incoming.end()) ||
        (source != InputNodeId && std::ranges::find(existing_destinations, destination) != existing_destinations.end())) {
        return false;
    }

    if (source_node != nullptr) {
        source_node->outgoing.push_back(destination);
    }
    if (destination == OutputNodeId) {
        output_incoming_.push_back(source);
    } else if (destination_node != nullptr) {
        destination_node->incoming.push_back(source);
    }
    prepared_ = false;
    return true;
}

std::vector<GraphPreparationError> AudioGraph::Prepare(const AudioFormat& format) {
    prepared_ = false;
    process_order_.clear();
    declared_latency_samples_ = 0;

    if (!format.Validate().empty()) {
        return {GraphPreparationError::InvalidAudioFormat};
    }

    std::vector<GraphPreparationError> errors;
    for (const auto& node : nodes_) {
        if (node.processor == nullptr) {
            errors.push_back(GraphPreparationError::MissingProcessor);
        }
    }
    if (!errors.empty()) {
        return errors;
    }

    std::vector<std::uint32_t> incoming_counts(nodes_.size(), 0);
    std::deque<NodeId> ready_nodes;
    for (const auto& node : nodes_) {
        const auto internal_inputs = static_cast<std::uint32_t>(std::ranges::count_if(
            node.incoming, [](NodeId id) { return id >= 2; }));
        incoming_counts[node.id - 2U] = internal_inputs;
        if (internal_inputs == 0) {
            ready_nodes.push_back(node.id);
        }
    }

    while (!ready_nodes.empty()) {
        const auto node_id = ready_nodes.front();
        ready_nodes.pop_front();
        process_order_.push_back(node_id);

        const auto* node = FindNode(node_id);
        for (const auto destination : node->outgoing) {
            if (destination < 2) {
                continue;
            }
            auto& count = incoming_counts[destination - 2U];
            --count;
            if (count == 0) {
                ready_nodes.push_back(destination);
            }
        }
    }

    if (process_order_.size() != nodes_.size()) {
        return {GraphPreparationError::CycleDetected};
    }

    if (output_incoming_.empty()) {
        return {GraphPreparationError::MissingOutputRoute};
    }

    format_ = format;
    for (const auto node_id : process_order_) {
        auto* node = FindNode(node_id);
        node->input_buffer = AudioBuffer(format.channel_count, format.max_block_size);
        node->output_buffer = AudioBuffer(format.channel_count, format.max_block_size);
        if (!node->processor->Prepare(format)) {
            return {GraphPreparationError::ProcessorRejectedFormat};
        }

        std::uint32_t incoming_latency = 0;
        for (const auto source : node->incoming) {
            if (source >= 2U) {
                incoming_latency = std::max(incoming_latency, FindNode(source)->cumulative_latency_samples);
            }
        }
        node->cumulative_latency_samples = incoming_latency + node->processor->DeclaredLatencySamples();
    }

    for (const auto source : output_incoming_) {
        if (source >= 2U) {
            declared_latency_samples_ = std::max(declared_latency_samples_, FindNode(source)->cumulative_latency_samples);
        }
    }
    prepared_ = true;
    return {};
}

bool AudioGraph::Process(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept {
    if (!prepared_ || sample_count > format_.max_block_size || input.ChannelCount() != format_.channel_count ||
        output.ChannelCount() != format_.channel_count || input.Capacity() < sample_count || output.Capacity() < sample_count) {
        return false;
    }

    for (const auto node_id : process_order_) {
        auto* node = FindNode(node_id);
        node->input_buffer.Clear(sample_count);
        for (const auto source : node->incoming) {
            if (source == InputNodeId) {
                static_cast<void>(node->input_buffer.AddFrom(input, sample_count));
            } else if (source >= 2U) {
                static_cast<void>(node->input_buffer.AddFrom(FindNode(source)->output_buffer, sample_count));
            }
        }
        node->processor->Process(node->input_buffer, node->output_buffer, sample_count);
    }

    output.Clear(sample_count);
    for (const auto source : output_incoming_) {
        if (source >= 2U) {
            static_cast<void>(output.AddFrom(FindNode(source)->output_buffer, sample_count));
        }
    }
    return true;
}

std::uint32_t AudioGraph::DeclaredLatencySamples() const noexcept {
    return declared_latency_samples_;
}

AudioGraph::Node* AudioGraph::FindNode(NodeId id) noexcept {
    if (id < 2U) {
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(id - 2U);
    return index < nodes_.size() ? &nodes_[index] : nullptr;
}

const AudioGraph::Node* AudioGraph::FindNode(NodeId id) const noexcept {
    if (id < 2U) {
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(id - 2U);
    return index < nodes_.size() ? &nodes_[index] : nullptr;
}

bool AudioGraph::IsKnownNode(NodeId id) const noexcept {
    return id == InputNodeId || id == OutputNodeId || FindNode(id) != nullptr;
}

} // namespace solo_rig
