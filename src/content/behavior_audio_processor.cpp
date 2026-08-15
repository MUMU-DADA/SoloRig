#include "content/behavior_audio_processor.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace solo_rig {

namespace {

std::optional<std::size_t> InputIndex(std::string_view name) noexcept {
    if (name == "audio") return 0U;
    if (name == "control") return 1U;
    if (name == "detector") return 2U;
    if (name == "emitter") return 3U;
    return std::nullopt;
}

std::optional<std::size_t> OutputIndex(std::string_view name) noexcept {
    if (name == "audio") return 0U;
    if (name == "control") return 1U;
    if (name == "resistance") return 2U;
    return std::nullopt;
}

} // namespace

TypedBehaviorPortProcessor::TypedBehaviorPortProcessor(
    BehaviorTypedGraphPlan plan, std::uint32_t oversampling)
    : plan_(std::move(plan)), requested_oversampling_(oversampling) {
    input_indices_.fill(-1);
    output_indices_.fill(-1);
    valid_ = true;
    for (const auto& route : plan_.routes) {
        if (route.source == "input") {
            const auto index = InputIndex(route.source_port);
            if (!index.has_value()) {
                valid_ = false;
            } else {
                input_indices_[*index] = 0;
                AddInput(route.source_port, InputDomain(route.source_port));
            }
        }
        if (route.destination == "output") {
            const auto index = OutputIndex(route.destination_port);
            if (!index.has_value()) {
                valid_ = false;
            } else {
                output_indices_[*index] = 0;
                AddOutput(route.destination_port, OutputDomain(route.destination_port));
            }
        }
    }
    for (std::size_t index = 0U; index < inputs_.count; ++index) {
        const auto port = InputIndex(inputs_.ports[index].name);
        if (port.has_value()) input_indices_[*port] = static_cast<std::int32_t>(index);
    }
    for (std::size_t index = 0U; index < outputs_.count; ++index) {
        const auto port = OutputIndex(outputs_.ports[index].name);
        if (port.has_value()) output_indices_[*port] = static_cast<std::int32_t>(index);
    }
}

NamedAudioPortDomain TypedBehaviorPortProcessor::InputDomain(std::string_view name) noexcept {
    if (name == "control") return NamedAudioPortDomain::ControlSignal;
    if (name == "detector") return NamedAudioPortDomain::DetectorInput;
    if (name == "emitter") return NamedAudioPortDomain::EmitterDrive;
    return NamedAudioPortDomain::AudioSignal;
}

NamedAudioPortDomain TypedBehaviorPortProcessor::OutputDomain(std::string_view name) noexcept {
    if (name == "control") return NamedAudioPortDomain::ControlSignal;
    if (name == "resistance") return NamedAudioPortDomain::Resistance;
    return NamedAudioPortDomain::AudioSignal;
}

void TypedBehaviorPortProcessor::AddInput(
    std::string_view name, NamedAudioPortDomain domain) noexcept {
    for (std::size_t index = 0U; index < inputs_.count; ++index) {
        if (inputs_.ports[index].name == name) {
            if (inputs_.ports[index].domain != domain) valid_ = false;
            return;
        }
    }
    if (inputs_.count >= inputs_.ports.size()) {
        valid_ = false;
        return;
    }
    inputs_.ports[inputs_.count++] = {name, NamedAudioPortDirection::Input, domain, 1U};
}

void TypedBehaviorPortProcessor::AddOutput(
    std::string_view name, NamedAudioPortDomain domain) noexcept {
    for (std::size_t index = 0U; index < outputs_.count; ++index) {
        if (outputs_.ports[index].name == name) {
            if (outputs_.ports[index].domain != domain) valid_ = false;
            return;
        }
    }
    if (outputs_.count >= outputs_.ports.size()) {
        valid_ = false;
        return;
    }
    outputs_.ports[outputs_.count++] = {name, NamedAudioPortDirection::Output, domain, 1U};
}

bool TypedBehaviorPortProcessor::Prepare(const AudioFormat& format,
                                         std::uint32_t oversampling) {
    prepared_ = false;
    if (!valid_ || format.channel_count != 1U || oversampling == 0U ||
        oversampling != requested_oversampling_) return false;
    const auto result = runtime_.Prepare(plan_, format.sample_rate_hz, oversampling);
    prepared_ = result == TypedBehaviorRuntimeError::None;
    return prepared_;
}

bool TypedBehaviorPortProcessor::Process(
    std::span<const NamedAudioPortInput> inputs,
    std::span<NamedAudioPortOutput> outputs,
    std::uint32_t sample_count) noexcept {
    if (!prepared_ || inputs.size() != inputs_.count || outputs.size() != outputs_.count) return false;
    for (std::uint32_t sample = 0U; sample < sample_count; ++sample) {
        TypedBehaviorInput input{};
        const auto read_input = [&](std::int32_t index) noexcept {
            return index < 0 ? 0.0 : static_cast<double>(inputs[static_cast<std::size_t>(index)].buffer->ChannelData(0U)[sample]);
        };
        input.audio = read_input(input_indices_[0]);
        input.control = read_input(input_indices_[1]);
        input.detector = read_input(input_indices_[2]);
        input.emitter = read_input(input_indices_[3]);
        TypedBehaviorOutput output{};
        if (runtime_.Process(input, output) != TypedBehaviorRuntimeError::None) return false;
        if (output_indices_[0] >= 0) outputs[static_cast<std::size_t>(output_indices_[0])].buffer->ChannelData(0U)[sample] =
            static_cast<float>(output.audio);
        if (output_indices_[1] >= 0) outputs[static_cast<std::size_t>(output_indices_[1])].buffer->ChannelData(0U)[sample] =
            static_cast<float>(output.control);
        if (output_indices_[2] >= 0) outputs[static_cast<std::size_t>(output_indices_[2])].buffer->ChannelData(0U)[sample] =
            static_cast<float>(output.resistance);
    }
    return true;
}

void TypedBehaviorPortProcessor::Reset() noexcept {
    runtime_.Reset();
}

TypedBehaviorMnaPortProcessor::TypedBehaviorMnaPortProcessor(
    BehaviorTypedGraphPlan plan, MnaPreparedCircuit circuit,
    std::span<const TypedBehaviorMnaBinding> bindings, std::uint32_t oversampling)
    : plan_(std::move(plan)), circuit_(std::move(circuit)), requested_oversampling_(oversampling) {
    input_indices_.fill(-1);
    valid_ = bindings.size() <= bindings_.size();
    binding_count_ = std::min(bindings.size(), bindings_.size());
    for (std::size_t index = 0U; index < binding_count_; ++index) bindings_[index] = bindings[index];
    // The MNA composite always consumes the circuit signal input directly;
    // it is independent of whether the behavior subgraph itself declares an
    // audio boundary.
    AddInput("audio", NamedAudioPortDomain::AudioSignal);
    for (const auto& route : plan_.routes) {
        if (route.source == "input") {
            const auto index = InputIndex(route.source_port);
            if (!index.has_value()) valid_ = false;
            else AddInput(route.source_port,
                          route.source_port == "control" ? NamedAudioPortDomain::ControlSignal :
                          route.source_port == "detector" ? NamedAudioPortDomain::DetectorInput :
                          route.source_port == "emitter" ? NamedAudioPortDomain::EmitterDrive :
                              NamedAudioPortDomain::AudioSignal);
        }
    }
    outputs_.ports[0] = {"audio", NamedAudioPortDirection::Output,
                         NamedAudioPortDomain::AudioSignal, 1U};
    outputs_.count = 1U;
    for (std::size_t index = 0U; index < inputs_.count; ++index) {
        const auto port = InputIndex(inputs_.ports[index].name);
        if (port.has_value()) input_indices_[*port] = static_cast<std::int32_t>(index);
    }
}

void TypedBehaviorMnaPortProcessor::AddInput(
    std::string_view name, NamedAudioPortDomain domain) noexcept {
    for (std::size_t index = 0U; index < inputs_.count; ++index) {
        if (inputs_.ports[index].name == name) {
            if (inputs_.ports[index].domain != domain) valid_ = false;
            return;
        }
    }
    if (inputs_.count >= inputs_.ports.size()) {
        valid_ = false;
        return;
    }
    inputs_.ports[inputs_.count++] = {name, NamedAudioPortDirection::Input, domain, 1U};
}

bool TypedBehaviorMnaPortProcessor::Prepare(const AudioFormat& format,
                                            std::uint32_t oversampling) {
    prepared_ = false;
    if (!valid_ || format.channel_count != 1U || oversampling == 0U ||
        oversampling != requested_oversampling_) return false;
    const auto result = runtime_.Prepare(plan_, std::move(circuit_),
        std::span<const TypedBehaviorMnaBinding>(bindings_.data(), binding_count_),
        format.sample_rate_hz, oversampling);
    prepared_ = result == TypedBehaviorMnaRuntimeError::None;
    return prepared_;
}

bool TypedBehaviorMnaPortProcessor::Process(
    std::span<const NamedAudioPortInput> inputs,
    std::span<NamedAudioPortOutput> outputs,
    std::uint32_t sample_count) noexcept {
    if (!prepared_ || inputs.size() != inputs_.count || outputs.size() != outputs_.count) return false;
    for (std::uint32_t sample = 0U; sample < sample_count; ++sample) {
        TypedBehaviorInput input{};
        const auto read_input = [&](std::int32_t index) noexcept {
            return index < 0 ? 0.0 : static_cast<double>(inputs[static_cast<std::size_t>(index)].buffer->ChannelData(0U)[sample]);
        };
        input.audio = read_input(input_indices_[0]);
        input.control = read_input(input_indices_[1]);
        input.detector = read_input(input_indices_[2]);
        input.emitter = read_input(input_indices_[3]);
        double output{};
        if (runtime_.Process(input, output) != TypedBehaviorMnaRuntimeError::None) return false;
        outputs[0].buffer->ChannelData(0U)[sample] = static_cast<float>(output);
    }
    return true;
}

void TypedBehaviorMnaPortProcessor::Reset() noexcept {
    runtime_.Reset();
}

} // namespace solo_rig
