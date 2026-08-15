#include "core/audio_graph.hpp"

#include "content/module_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <deque>
#include <limits>
#include <new>

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
    output_routes_.clear();
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
        node->performance_meter.Configure(format.sample_rate_hz, format.max_block_size);

        std::uint32_t incoming_latency = 0;
        for (const auto source : node->incoming) {
            if (source >= 2U) {
                incoming_latency = std::max(incoming_latency, FindNode(source)->cumulative_latency_samples);
            }
        }
        if (node->processor->DeclaredLatencySamples() > std::numeric_limits<std::uint32_t>::max() - incoming_latency) {
            return {GraphPreparationError::LatencyOverflow};
        }
        node->input_latency_samples = incoming_latency;
        node->cumulative_latency_samples = incoming_latency + node->processor->DeclaredLatencySamples();
    }

    for (const auto source : output_incoming_) {
        if (source >= 2U) {
            declared_latency_samples_ = std::max(declared_latency_samples_, FindNode(source)->cumulative_latency_samples);
        }
    }

    for (const auto node_id : process_order_) {
        auto* node = FindNode(node_id);
        node->input_routes.clear();
        node->input_routes.reserve(node->incoming.size());
        for (const auto source : node->incoming) {
            const auto source_latency = source >= 2U ? FindNode(source)->cumulative_latency_samples : 0U;
            Route route{.source = source, .compensation = {.delay_samples = node->input_latency_samples - source_latency}};
            if (!route.compensation.Prepare(format.channel_count)) {
                return {GraphPreparationError::LatencyCompensationAllocationFailed};
            }
            node->input_routes.push_back(std::move(route));
        }
    }

    output_routes_.reserve(output_incoming_.size());
    for (const auto source : output_incoming_) {
        if (source < 2U) {
            continue;
        }
        Route route{.source = source,
                    .compensation = {.delay_samples = declared_latency_samples_ - FindNode(source)->cumulative_latency_samples}};
        if (!route.compensation.Prepare(format.channel_count)) {
            return {GraphPreparationError::LatencyCompensationAllocationFailed};
        }
        output_routes_.push_back(std::move(route));
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
        for (auto& route : node->input_routes) {
            const auto& source = route.source == InputNodeId ? input : FindNode(route.source)->output_buffer;
            route.compensation.AddFrom(source, node->input_buffer, sample_count);
        }
        const auto started = std::chrono::steady_clock::now();
        node->processor->Process(node->input_buffer, node->output_buffer, sample_count);
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count();
        node->performance_meter.RecordProcessingNanoseconds(
            elapsed < 0 ? 0U : static_cast<std::uint64_t>(elapsed));
    }

    output.Clear(sample_count);
    for (auto& route : output_routes_) {
        route.compensation.AddFrom(FindNode(route.source)->output_buffer, output, sample_count);
    }
    return true;
}

bool AudioGraph::SetParameter(NodeId node_id, std::string_view parameter_id, float value) noexcept {
    auto* node = FindNode(node_id);
    if (node == nullptr || node->processor == nullptr) {
        return false;
    }
    auto* parameterized = dynamic_cast<ModuleRuntimeProcessor*>(node->processor.get());
    return parameterized != nullptr && parameterized->SetParameter(parameter_id, value);
}

std::uint32_t AudioGraph::DeclaredLatencySamples() const noexcept {
    return declared_latency_samples_;
}

std::vector<AudioGraph::NodePerformance> AudioGraph::PerformanceSnapshot() const {
    std::vector<NodePerformance> result;
    result.reserve(nodes_.size());
    for (const auto& node : nodes_) {
        result.push_back({.node_id = node.id,
                          .declared_latency_samples = node.processor == nullptr
                              ? 0U : node.processor->DeclaredLatencySamples(),
                          .last_cpu_percent = node.performance_meter.LastLoadFraction() * 100.0,
                          .peak_cpu_percent = node.performance_meter.PeakLoadFraction() * 100.0});
    }
    return result;
}

bool AudioGraph::DelayCompensator::Prepare(std::uint32_t channel_count) {
    write_position = 0;
    if (delay_samples == 0U) {
        delay_line.clear();
        return true;
    }
    const auto line_size = static_cast<std::size_t>(channel_count) * delay_samples;
    if (channel_count == 0U || line_size / channel_count != delay_samples) {
        return false;
    }
    try {
        delay_line.assign(line_size, 0.0F);
    } catch (const std::bad_alloc&) {
        return false;
    }
    return true;
}

void AudioGraph::DelayCompensator::AddFrom(const AudioBuffer& source,
                                           AudioBuffer& destination,
                                           std::uint32_t sample_count) noexcept {
    if (delay_samples == 0U) {
        static_cast<void>(destination.AddFrom(source, sample_count));
        return;
    }

    for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
        for (std::uint32_t channel = 0; channel < source.ChannelCount(); ++channel) {
            const auto index = static_cast<std::size_t>(channel) * delay_samples + write_position;
            destination.ChannelData(channel)[sample] += delay_line[index];
            delay_line[index] = source.ChannelData(channel)[sample];
        }
        write_position = (write_position + 1U) % delay_samples;
    }
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
