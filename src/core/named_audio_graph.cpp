#include "core/named_audio_graph.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace solo_rig {

bool NamedPortAudioGraph::AddNode(
    std::string node_id, std::unique_ptr<NamedAudioPortProcessor> processor) {
    if (node_id.empty() || node_id == "input" || node_id == "output" || processor == nullptr ||
        nodes_.size() >= kMaximumNamedAudioNodes || FindNode(node_id).has_value()) {
        return false;
    }
    nodes_.push_back({.id = std::move(node_id), .processor = std::move(processor)});
    prepared_ = false;
    return true;
}

bool NamedPortAudioGraph::AddInputPort(std::string name, NamedAudioPortDomain domain,
                                       std::uint32_t channel_count) {
    if (name.empty() || name == "input" || name == "output" ||
        input_ports_.size() >= kMaximumNamedAudioPorts ||
        FindInputPort(name).has_value()) {
        return false;
    }
    input_ports_.push_back({.contract = {.name = {},
                                         .direction = NamedAudioPortDirection::Output,
                                         .domain = domain,
                                         .channel_count = channel_count},
                            .name = std::move(name)});
    for (auto& port : input_ports_) port.contract.name = port.name;
    prepared_ = false;
    return true;
}

bool NamedPortAudioGraph::AddOutputPort(std::string name, NamedAudioPortDomain domain,
                                        std::uint32_t channel_count) {
    if (name.empty() || name == "input" || name == "output" ||
        output_ports_.size() >= kMaximumNamedAudioPorts ||
        FindOutputPort(name).has_value()) {
        return false;
    }
    output_ports_.push_back({.contract = {.name = {},
                                          .direction = NamedAudioPortDirection::Input,
                                          .domain = domain,
                                          .channel_count = channel_count},
                             .name = std::move(name)});
    for (auto& port : output_ports_) port.contract.name = port.name;
    prepared_ = false;
    return true;
}

bool NamedPortAudioGraph::Connect(NamedAudioPortRouteSpec route) {
    if (route.source.empty() || route.source_port.empty() || route.destination.empty() ||
        route.destination_port.empty() || route.source == "output" || route.destination == "input" ||
        (route.source == route.destination && !route.delayed) ||
        route_specs_.size() >= kMaximumNamedAudioRoutes) {
        return false;
    }
    for (const auto& prior : route_specs_) {
        if (prior.source == route.source && prior.source_port == route.source_port &&
            prior.destination == route.destination && prior.destination_port == route.destination_port) {
            return false;
        }
    }
    route_specs_.push_back(std::move(route));
    prepared_ = false;
    return true;
}

std::vector<NamedAudioGraphPreparationError> NamedPortAudioGraph::Prepare(
    const AudioFormat& format, std::uint32_t oversampling) {
    prepared_ = false;
    routes_.clear();
    process_order_.clear();
    declared_latency_samples_ = 0U;
    if (!format.Validate().empty()) return {NamedAudioGraphPreparationError::InvalidAudioFormat};
    if (oversampling == 0U || oversampling > 16U) {
        return {NamedAudioGraphPreparationError::InvalidOversampling};
    }
    if (nodes_.empty() || nodes_.size() > kMaximumNamedAudioNodes ||
        input_ports_.empty() || output_ports_.empty() ||
        route_specs_.empty() || route_specs_.size() > kMaximumNamedAudioRoutes) {
        return {NamedAudioGraphPreparationError::CapacityExceeded};
    }

    std::vector<std::array<std::size_t, kMaximumNamedAudioPorts>> incoming_counts(nodes_.size());
    std::vector<std::array<std::size_t, kMaximumNamedAudioPorts>> outgoing_counts(nodes_.size());
    std::vector<std::size_t> input_counts(input_ports_.size(), 0U);
    std::vector<std::size_t> output_counts(output_ports_.size(), 0U);
    for (auto& node : nodes_) {
        node.incoming_routes.clear();
        node.outgoing_routes.clear();
        node.input_contracts = node.processor->InputPorts();
        node.output_contracts = node.processor->OutputPorts();
        if (node.input_contracts.count > kMaximumNamedAudioPorts ||
            node.output_contracts.count > kMaximumNamedAudioPorts) {
            return {NamedAudioGraphPreparationError::CapacityExceeded};
        }
        for (std::size_t index = 0U; index < node.input_contracts.count; ++index) {
            const auto& port = node.input_contracts.ports[index];
            bool duplicate = false;
            for (std::size_t prior = 0U; prior < index; ++prior) {
                duplicate = duplicate || node.input_contracts.ports[prior].name == port.name;
            }
            if (port.name.empty() || duplicate) {
                return {port.name.empty() ? NamedAudioGraphPreparationError::InvalidPort :
                    NamedAudioGraphPreparationError::DuplicatePort};
            }
        }
        for (std::size_t index = 0U; index < node.output_contracts.count; ++index) {
            const auto& port = node.output_contracts.ports[index];
            bool duplicate = false;
            for (std::size_t prior = 0U; prior < index; ++prior) {
                duplicate = duplicate || node.output_contracts.ports[prior].name == port.name;
            }
            if (port.name.empty() || duplicate) {
                return {port.name.empty() ? NamedAudioGraphPreparationError::InvalidPort :
                    NamedAudioGraphPreparationError::DuplicatePort};
            }
        }
        if (!node.processor->Prepare(format, oversampling)) {
            return {NamedAudioGraphPreparationError::ProcessorRejectedFormat};
        }
        for (std::size_t index = 0U; index < node.input_contracts.count; ++index) {
            auto& port = node.input_contracts.ports[index];
            port.channel_count = ResolveChannelCount(port, format);
            if (port.channel_count == 0U) return {NamedAudioGraphPreparationError::InvalidPort};
            node.input_buffers[index] = AudioBuffer(port.channel_count, format.max_block_size);
            node.input_views[index] = {port.name, port.domain, &node.input_buffers[index]};
        }
        for (std::size_t index = 0U; index < node.output_contracts.count; ++index) {
            auto& port = node.output_contracts.ports[index];
            port.channel_count = ResolveChannelCount(port, format);
            if (port.channel_count == 0U) return {NamedAudioGraphPreparationError::InvalidPort};
            node.output_buffers[index] = AudioBuffer(port.channel_count, format.max_block_size);
            node.output_views[index] = {port.name, port.domain, &node.output_buffers[index]};
        }
    }

    for (auto& port : input_ports_) {
        port.contract.name = port.name;
        for (const auto& other : input_ports_) {
            if (&port != &other && port.name == other.name) {
                return {NamedAudioGraphPreparationError::DuplicateBoundaryPort};
            }
        }
        if (port.contract.channel_count != 0U && port.contract.channel_count > format.channel_count) {
            return {NamedAudioGraphPreparationError::ChannelMismatch};
        }
    }
    for (auto& port : input_ports_) {
        port.contract.channel_count = ResolveChannelCount(port.contract, format);
    }
    for (auto& port : output_ports_) {
        port.contract.name = port.name;
        for (const auto& other : output_ports_) {
            if (&port != &other && port.name == other.name) {
                return {NamedAudioGraphPreparationError::DuplicateBoundaryPort};
            }
        }
        if (port.contract.channel_count != 0U && port.contract.channel_count > format.channel_count) {
            return {NamedAudioGraphPreparationError::ChannelMismatch};
        }
        port.contract.channel_count = ResolveChannelCount(port.contract, format);
    }

    std::vector<std::size_t> indegree(nodes_.size(), 0U);
    std::vector<bool> dependencies(nodes_.size() * nodes_.size(), false);
    routes_.reserve(route_specs_.size());
    for (const auto& spec : route_specs_) {
        Route route{.delayed = spec.delayed};
        if (spec.source == "input") {
            const auto source = FindInputPort(spec.source_port);
            if (!source.has_value()) return {NamedAudioGraphPreparationError::UnknownPort};
            route.source_is_input = true;
            route.source_boundary_index = *source;
            ++input_counts[*source];
            if (input_counts[*source] > kMaximumNamedRoutesPerPort) {
                return {NamedAudioGraphPreparationError::CapacityExceeded};
            }
        } else {
            const auto source = FindNode(spec.source);
            if (!source.has_value()) return {NamedAudioGraphPreparationError::UnknownEndpoint};
            const auto port = FindPort(nodes_[*source].output_contracts, spec.source_port);
            if (!port.has_value()) return {NamedAudioGraphPreparationError::UnknownPort};
            route.source_node_index = *source;
            route.source_port_index = *port;
            ++outgoing_counts[*source][*port];
            if (outgoing_counts[*source][*port] > kMaximumNamedRoutesPerPort) {
                return {NamedAudioGraphPreparationError::CapacityExceeded};
            }
        }
        if (spec.destination == "output") {
            const auto destination = FindOutputPort(spec.destination_port);
            if (!destination.has_value()) return {NamedAudioGraphPreparationError::UnknownPort};
            if (spec.delayed) return {NamedAudioGraphPreparationError::DelayedBoundaryRoute};
            route.destination_is_output = true;
            route.destination_boundary_index = *destination;
            ++output_counts[*destination];
            if (output_counts[*destination] > kMaximumNamedRoutesPerPort) {
                return {NamedAudioGraphPreparationError::CapacityExceeded};
            }
        } else {
            const auto destination = FindNode(spec.destination);
            if (!destination.has_value()) return {NamedAudioGraphPreparationError::UnknownEndpoint};
            const auto port = FindPort(nodes_[*destination].input_contracts, spec.destination_port);
            if (!port.has_value()) return {NamedAudioGraphPreparationError::UnknownPort};
            route.destination_node_index = *destination;
            route.destination_port_index = *port;
            ++incoming_counts[*destination][*port];
            if (incoming_counts[*destination][*port] > kMaximumNamedRoutesPerPort) {
                return {NamedAudioGraphPreparationError::CapacityExceeded};
            }
        }

        const auto source_domain = route.source_is_input
            ? input_ports_[route.source_boundary_index].contract.domain
            : nodes_[route.source_node_index].output_contracts.ports[route.source_port_index].domain;
        const auto destination_domain = route.destination_is_output
            ? output_ports_[route.destination_boundary_index].contract.domain
            : nodes_[route.destination_node_index].input_contracts.ports[route.destination_port_index].domain;
        if (!Compatible(source_domain, destination_domain)) {
            return {NamedAudioGraphPreparationError::IncompatibleDomain};
        }
        const auto source_channels = route.source_is_input
            ? input_ports_[route.source_boundary_index].contract.channel_count
            : nodes_[route.source_node_index].output_contracts.ports[route.source_port_index].channel_count;
        const auto destination_channels = route.destination_is_output
            ? output_ports_[route.destination_boundary_index].contract.channel_count
            : nodes_[route.destination_node_index].input_contracts.ports[route.destination_port_index].channel_count;
        if (source_channels != destination_channels) {
            return {NamedAudioGraphPreparationError::ChannelMismatch};
        }
        if (route.delayed && (route.source_is_input || route.destination_is_output)) {
            return {NamedAudioGraphPreparationError::DelayedBoundaryRoute};
        }
        if (!route.source_is_input && !route.destination_is_output && !route.delayed) {
            const auto dependency = route.source_node_index * nodes_.size() + route.destination_node_index;
            if (!dependencies[dependency]) {
                dependencies[dependency] = true;
                ++indegree[route.destination_node_index];
            }
        }
        const auto route_index = routes_.size();
        routes_.push_back(std::move(route));
        if (!routes_.back().source_is_input) {
            nodes_[routes_.back().source_node_index].outgoing_routes.push_back(route_index);
        }
        if (!routes_.back().destination_is_output) {
            nodes_[routes_.back().destination_node_index].incoming_routes.push_back(route_index);
        }
    }

    for (std::size_t index = 0U; index < input_ports_.size(); ++index) {
        if (input_counts[index] == 0U) return {NamedAudioGraphPreparationError::MissingInputRoute};
    }
    for (std::size_t index = 0U; index < output_ports_.size(); ++index) {
        if (output_counts[index] == 0U) return {NamedAudioGraphPreparationError::MissingOutputRoute};
    }
    for (std::size_t node = 0U; node < nodes_.size(); ++node) {
        for (std::size_t port = 0U; port < nodes_[node].input_contracts.count; ++port) {
            if (incoming_counts[node][port] == 0U) return {NamedAudioGraphPreparationError::MissingPort};
        }
        for (std::size_t port = 0U; port < nodes_[node].output_contracts.count; ++port) {
            if (outgoing_counts[node][port] == 0U) return {NamedAudioGraphPreparationError::MissingPort};
        }
    }

    std::vector<std::size_t> ready;
    for (std::size_t index = 0U; index < nodes_.size(); ++index) {
        if (indegree[index] == 0U) ready.push_back(index);
    }
    while (!ready.empty()) {
        const auto node = ready.front();
        ready.erase(ready.begin());
        process_order_.push_back(node);
        for (const auto route_index : nodes_[node].outgoing_routes) {
            const auto& route = routes_[route_index];
            if (route.destination_is_output || route.delayed) continue;
            const auto dependency = node * nodes_.size() + route.destination_node_index;
            if (dependencies[dependency]) {
                dependencies[dependency] = false;
                if (--indegree[route.destination_node_index] == 0U) {
                    ready.push_back(route.destination_node_index);
                }
            }
        }
    }
    if (process_order_.size() != nodes_.size()) {
        return {NamedAudioGraphPreparationError::CycleDetected};
    }

    format_ = format;
    oversampling_ = oversampling;
    for (const auto node_index : process_order_) {
        auto& node = nodes_[node_index];
        std::uint32_t incoming_latency = 0U;
        for (const auto route_index : node.incoming_routes) {
            const auto& route = routes_[route_index];
            if (route.delayed) continue;
            const auto source_latency = route.source_is_input
                ? 0U : nodes_[route.source_node_index].cumulative_latency_samples;
            incoming_latency = std::max(incoming_latency, source_latency);
        }
        const auto processor_latency = node.processor->DeclaredLatencySamples();
        if (processor_latency > std::numeric_limits<std::uint32_t>::max() - incoming_latency) {
            return {NamedAudioGraphPreparationError::LatencyOverflow};
        }
        node.input_latency_samples = incoming_latency;
        node.cumulative_latency_samples = incoming_latency + processor_latency;
        for (const auto route_index : node.incoming_routes) {
            auto& route = routes_[route_index];
            const auto channels = node.input_contracts.ports[route.destination_port_index].channel_count;
            if (route.delayed) {
                route.delayed_buffer = AudioBuffer(channels, format.max_block_size);
                route.delayed_buffer.Clear(format.max_block_size);
                continue;
            }
            const auto source_latency = route.source_is_input
                ? 0U : nodes_[route.source_node_index].cumulative_latency_samples;
            route.compensation.delay_samples = node.input_latency_samples - source_latency;
            if (!route.compensation.Prepare(channels, format.max_block_size)) {
                return {NamedAudioGraphPreparationError::BufferAllocationFailed};
            }
        }
    }
    for (const auto& route : routes_) {
        if (!route.destination_is_output) continue;
        const auto source_latency = route.source_is_input
            ? 0U : nodes_[route.source_node_index].cumulative_latency_samples;
        declared_latency_samples_ = std::max(declared_latency_samples_, source_latency);
    }
    for (auto& route : routes_) {
        if (!route.destination_is_output) continue;
        const auto channels = output_ports_[route.destination_boundary_index].contract.channel_count;
        const auto source_latency = route.source_is_input
            ? 0U : nodes_[route.source_node_index].cumulative_latency_samples;
        route.compensation.delay_samples = declared_latency_samples_ - source_latency;
        if (!route.compensation.Prepare(channels, format.max_block_size)) {
            return {NamedAudioGraphPreparationError::BufferAllocationFailed};
        }
    }
    prepared_ = true;
    Reset();
    return {};
}

bool NamedPortAudioGraph::Process(std::span<const AudioBuffer> inputs,
                                  std::span<AudioBuffer> outputs,
                                  std::uint32_t sample_count) noexcept {
    if (!prepared_ || sample_count > format_.max_block_size || inputs.size() != input_ports_.size() ||
        outputs.size() != output_ports_.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < inputs.size(); ++index) {
        if (!inputs[index].IsValid() || inputs[index].ChannelCount() != input_ports_[index].contract.channel_count ||
            inputs[index].Capacity() < sample_count) return false;
    }
    for (std::size_t index = 0U; index < outputs.size(); ++index) {
        if (!outputs[index].IsValid() || outputs[index].ChannelCount() != output_ports_[index].contract.channel_count ||
            outputs[index].Capacity() < sample_count) return false;
        outputs[index].Clear(sample_count);
    }

    for (const auto node_index : process_order_) {
        auto& node = nodes_[node_index];
        for (std::size_t port = 0U; port < node.input_contracts.count; ++port) {
            node.input_buffers[port].Clear(sample_count);
        }
        for (const auto route_index : node.incoming_routes) {
            auto& route = routes_[route_index];
            const auto* source = SourceBuffer(route, inputs);
            if (source == nullptr) return false;
            route.compensation.AddFrom(*source,
                node.input_buffers[route.destination_port_index], sample_count);
        }
        for (std::size_t port = 0U; port < node.output_contracts.count; ++port) {
            node.output_buffers[port].Clear(sample_count);
        }
        if (!node.processor->Process(
                std::span<const NamedAudioPortInput>(node.input_views.data(), node.input_contracts.count),
                std::span<NamedAudioPortOutput>(node.output_views.data(), node.output_contracts.count),
                sample_count)) {
            return false;
        }
    }

    for (auto& route : routes_) {
        if (!route.destination_is_output) continue;
        const auto* source = SourceBuffer(route, inputs);
        if (source == nullptr) return false;
        route.compensation.AddFrom(*source, outputs[route.destination_boundary_index], sample_count);
    }
    for (auto& route : routes_) {
        if (!route.delayed) continue;
        if (route.source_is_input || route.source_node_index == kMissing) return false;
        const auto& source = nodes_[route.source_node_index].output_buffers[route.source_port_index];
        if (!route.delayed_buffer.CopyFrom(source, sample_count)) return false;
    }
    return true;
}

void NamedPortAudioGraph::Reset() noexcept {
    if (!prepared_) return;
    for (auto& node : nodes_) {
        node.processor->Reset();
        for (std::size_t port = 0U; port < node.input_contracts.count; ++port) {
            node.input_buffers[port].Clear(format_.max_block_size);
        }
        for (std::size_t port = 0U; port < node.output_contracts.count; ++port) {
            node.output_buffers[port].Clear(format_.max_block_size);
        }
    }
    for (auto& route : routes_) {
        route.compensation.Reset();
        if (route.delayed) route.delayed_buffer.Clear(format_.max_block_size);
    }
}

const NamedAudioPortContract& NamedPortAudioGraph::InputPort(std::size_t index) const noexcept {
    static const NamedAudioPortContract empty{};
    return index < input_ports_.size() ? input_ports_[index].contract : empty;
}

const NamedAudioPortContract& NamedPortAudioGraph::OutputPort(std::size_t index) const noexcept {
    static const NamedAudioPortContract empty{};
    return index < output_ports_.size() ? output_ports_[index].contract : empty;
}

std::optional<std::size_t> NamedPortAudioGraph::FindNode(std::string_view id) const noexcept {
    for (std::size_t index = 0U; index < nodes_.size(); ++index) {
        if (nodes_[index].id == id) return index;
    }
    return std::nullopt;
}

std::optional<std::size_t> NamedPortAudioGraph::FindInputPort(std::string_view name) const noexcept {
    for (std::size_t index = 0U; index < input_ports_.size(); ++index) {
        if (input_ports_[index].name == name) return index;
    }
    return std::nullopt;
}

std::optional<std::size_t> NamedPortAudioGraph::FindOutputPort(std::string_view name) const noexcept {
    for (std::size_t index = 0U; index < output_ports_.size(); ++index) {
        if (output_ports_[index].name == name) return index;
    }
    return std::nullopt;
}

std::optional<std::size_t> NamedPortAudioGraph::FindPort(
    const NamedAudioPortContractList& ports, std::string_view name) noexcept {
    for (std::size_t index = 0U; index < ports.count; ++index) {
        if (ports.ports[index].name == name) return index;
    }
    return std::nullopt;
}

bool NamedPortAudioGraph::Compatible(NamedAudioPortDomain source,
                                     NamedAudioPortDomain destination) noexcept {
    if (source == destination) return true;
    return source == NamedAudioPortDomain::AudioSignal &&
        (destination == NamedAudioPortDomain::DetectorInput ||
         destination == NamedAudioPortDomain::EmitterDrive);
}

std::uint32_t NamedPortAudioGraph::ResolveChannelCount(
    const NamedAudioPortContract& contract, const AudioFormat& format) noexcept {
    return contract.channel_count == 0U ? format.channel_count : contract.channel_count;
}

const AudioBuffer* NamedPortAudioGraph::SourceBuffer(
    const Route& route, std::span<const AudioBuffer> inputs) const noexcept {
    if (route.delayed) return &route.delayed_buffer;
    if (route.source_is_input) {
        return route.source_boundary_index < inputs.size() ? &inputs[route.source_boundary_index] : nullptr;
    }
    if (route.source_node_index == kMissing || route.source_port_index == kMissing ||
        route.source_node_index >= nodes_.size()) return nullptr;
    return &nodes_[route.source_node_index].output_buffers[route.source_port_index];
}

bool NamedPortAudioGraph::DelayCompensator::Prepare(
    std::uint32_t channel_count, std::uint32_t capacity) noexcept {
    write_position = 0U;
    if (delay_samples == 0U) {
        delay_line = AudioBuffer{};
        return true;
    }
    if (channel_count == 0U || capacity == 0U) return false;
    try {
        delay_line = AudioBuffer(channel_count, delay_samples);
    } catch (const std::bad_alloc&) {
        return false;
    }
    static_cast<void>(capacity);
    return delay_line.IsValid();
}

void NamedPortAudioGraph::DelayCompensator::Reset() noexcept {
    write_position = 0U;
    if (delay_line.IsValid()) delay_line.Clear(delay_line.Capacity());
}

void NamedPortAudioGraph::DelayCompensator::AddFrom(
    const AudioBuffer& source, AudioBuffer& destination, std::uint32_t sample_count) noexcept {
    if (delay_samples == 0U) {
        static_cast<void>(destination.AddFrom(source, sample_count));
        return;
    }
    for (std::uint32_t sample = 0U; sample < sample_count; ++sample) {
        for (std::uint32_t channel = 0U; channel < source.ChannelCount(); ++channel) {
            destination.ChannelData(channel)[sample] += delay_line.ChannelData(channel)[write_position];
            delay_line.ChannelData(channel)[write_position] = source.ChannelData(channel)[sample];
        }
        write_position = (write_position + 1U) % delay_samples;
    }
}

} // namespace solo_rig
