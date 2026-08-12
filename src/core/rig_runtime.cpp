#include "core/rig_runtime.hpp"

#include "dsp/digital_delay.hpp"
#include "dsp/diode_clipper.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <ranges>
#include <utility>

namespace solo_rig {

namespace {

class BuiltinDiodeProcessor final : public ModuleRuntimeProcessor {
public:
    BuiltinDiodeProcessor(float drive, float output_gain, float threshold)
        : settings_{.drive = drive, .output_gain = output_gain, .diode_threshold = threshold}, clipper_(settings_) {}

    bool Prepare(const AudioFormat& format) override { return clipper_.Prepare(format); }

    void Process(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept override {
        clipper_.Process(input, output, sample_count);
    }

    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override {
        return clipper_.DeclaredLatencySamples();
    }

    [[nodiscard]] bool SetParameter(std::string_view parameter_id, float value) noexcept override {
        auto candidate = settings_;
        if (parameter_id == "drive") {
            candidate.drive = value;
        } else if (parameter_id == "outputGain") {
            candidate.output_gain = value;
        } else if (parameter_id == "diodeThreshold") {
            candidate.diode_threshold = value;
        } else {
            return false;
        }
        if (!clipper_.SetSettings(candidate)) {
            return false;
        }
        settings_ = candidate;
        return true;
    }

    [[nodiscard]] std::optional<float> ParameterValue(std::string_view parameter_id) const noexcept override {
        if (parameter_id == "drive") {
            return settings_.drive;
        }
        if (parameter_id == "outputGain") {
            return settings_.output_gain;
        }
        if (parameter_id == "diodeThreshold") {
            return settings_.diode_threshold;
        }
        return std::nullopt;
    }

private:
    DiodeClipperSettings settings_;
    DiodeClipper clipper_;
};

class BuiltinDelayProcessor final : public ModuleRuntimeProcessor {
public:
    BuiltinDelayProcessor(float time, float feedback, float mix)
        : settings_{.delay_seconds = time, .feedback = feedback, .wet = mix, .dry = 1.0F - mix}, delay_(2.0F, settings_) {}

    bool Prepare(const AudioFormat& format) override { return delay_.Prepare(format); }

    void Process(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept override {
        delay_.Process(input, output, sample_count);
    }

    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override {
        return delay_.DeclaredLatencySamples();
    }

    [[nodiscard]] bool SetParameter(std::string_view parameter_id, float value) noexcept override {
        auto candidate = settings_;
        if (parameter_id == "time") {
            candidate.delay_seconds = value;
        } else if (parameter_id == "feedback") {
            candidate.feedback = value;
        } else if (parameter_id == "mix") {
            candidate.wet = value;
            candidate.dry = 1.0F - value;
        } else {
            return false;
        }
        if (!delay_.SetSettings(candidate)) {
            return false;
        }
        settings_ = candidate;
        return true;
    }

    [[nodiscard]] std::optional<float> ParameterValue(std::string_view parameter_id) const noexcept override {
        if (parameter_id == "time") {
            return settings_.delay_seconds;
        }
        if (parameter_id == "feedback") {
            return settings_.feedback;
        }
        if (parameter_id == "mix") {
            return settings_.wet;
        }
        return std::nullopt;
    }

private:
    DigitalDelaySettings settings_;
    DigitalDelay delay_;
};

std::variant<std::unique_ptr<ModuleRuntimeProcessor>, RigBuildError> InstantiateBuiltinDrive(const PresetNode& node) {
    if (node.module_id != "org.mumu-dada.solorig.drive") {
        return RigBuildError::UnknownModuleId;
    }
    const float drive = node.parameter_values.contains("drive") ? node.parameter_values.at("drive") : 1.0F;
    const float gain = node.parameter_values.contains("outputGain") ? node.parameter_values.at("outputGain") : 1.0F;
    const float threshold = node.parameter_values.contains("diodeThreshold") ? node.parameter_values.at("diodeThreshold") : 0.35F;
    auto processor = std::make_unique<BuiltinDiodeProcessor>(drive, gain, threshold);
    if (!processor->SetParameter("drive", drive) || !processor->SetParameter("outputGain", gain) ||
        !processor->SetParameter("diodeThreshold", threshold)) {
        return RigBuildError::ParameterMismatch;
    }
    return std::unique_ptr<ModuleRuntimeProcessor>(std::move(processor));
}

std::variant<std::unique_ptr<ModuleRuntimeProcessor>, RigBuildError> InstantiateBuiltinDelay(const PresetNode& node) {
    if (node.module_id != "org.mumu-dada.solorig.digital-delay") {
        return RigBuildError::UnknownModuleId;
    }
    const float time = node.parameter_values.contains("time") ? node.parameter_values.at("time") : 0.35F;
    const float feedback = node.parameter_values.contains("feedback") ? node.parameter_values.at("feedback") : 0.35F;
    const float mix = node.parameter_values.contains("mix") ? node.parameter_values.at("mix") : 0.25F;
    auto processor = std::make_unique<BuiltinDelayProcessor>(time, feedback, mix);
    if (!processor->SetParameter("time", time) || !processor->SetParameter("feedback", feedback) ||
        !processor->SetParameter("mix", mix)) {
        return RigBuildError::ParameterMismatch;
    }
    return std::unique_ptr<ModuleRuntimeProcessor>(std::move(processor));
}

} // namespace

RigModuleRegistry::RigModuleRegistry() {
    static_cast<void>(Register("org.mumu-dada.solorig.drive", InstantiateBuiltinDrive));
    static_cast<void>(Register("org.mumu-dada.solorig.digital-delay", InstantiateBuiltinDelay));
}

std::optional<RigBuildError> RigModuleRegistry::Register(std::string module_id, RigModuleFactory factory) {
    if (module_id.empty() || !factory || factories_.contains(module_id) || packages_.contains(module_id)) {
        return module_id.empty() ? RigBuildError::EmptyModuleId : RigBuildError::ProcessorInstantiationFailed;
    }
    factories_.emplace(std::move(module_id), std::move(factory));
    return std::nullopt;
}

std::optional<RigBuildError> RigModuleRegistry::RegisterPackage(ModulePackage package) {
    if (package.descriptor.module_id.empty() || package.descriptor.Validate().size() != 0U) {
        return RigBuildError::MissingModuleAsset;
    }
    if (factories_.contains(package.descriptor.module_id) || packages_.contains(package.descriptor.module_id)) {
        return RigBuildError::ProcessorInstantiationFailed;
    }
    packages_.emplace(package.descriptor.module_id, std::move(package));
    return std::nullopt;
}

bool RigModuleRegistry::Contains(std::string_view module_id) const {
    return factories_.contains(module_id) || packages_.contains(module_id);
}

std::variant<std::unique_ptr<ModuleRuntimeProcessor>, RigBuildError> RigModuleRegistry::Instantiate(
    const PresetNode& node) const {
    if (node.module_id.empty()) {
        return RigBuildError::EmptyModuleId;
    }
    if (const auto factory = factories_.find(node.module_id); factory != factories_.end()) {
        auto created = factory->second(node);
        if (std::holds_alternative<RigBuildError>(created)) {
            return std::get<RigBuildError>(created);
        }
        auto processor = std::move(std::get<std::unique_ptr<ModuleRuntimeProcessor>>(created));
        for (const auto& [parameter_id, value] : node.parameter_values) {
            if (!processor->SetParameter(parameter_id, value)) {
                return RigBuildError::ParameterMismatch;
            }
        }
        return std::move(processor);
    }
    const auto package = packages_.find(node.module_id);
    if (package == packages_.end()) {
        return RigBuildError::UnknownModuleId;
    }
    auto runtime = CreateModuleRuntime(package->second);
    if (std::holds_alternative<ModuleRuntimeError>(runtime)) {
        return RigBuildError::ProcessorInstantiationFailed;
    }
    auto processor = std::move(std::get<std::unique_ptr<ModuleRuntimeProcessor>>(runtime));
    for (const auto& [parameter_id, value] : node.parameter_values) {
        if (!processor->SetParameter(parameter_id, value)) {
            return RigBuildError::ParameterMismatch;
        }
    }
    return std::move(processor);
}

std::variant<PreparedPreset, RigBuildError> RigRuntimeBuilder::Build(const PreparedPreset& prepared,
                                                                      const RigModuleRegistry& registry,
                                                                      const AudioFormat& format) const {
    if (!format.Validate().empty() || prepared.preset_id.empty()) {
        return RigBuildError::GraphPreparationFailed;
    }
    if (prepared.preset.nodes.empty()) {
        auto result = prepared;
        result.runtime_node_indices.clear();
        result.runtime_graph.reset();
        return result;
    }

    auto graph = std::make_shared<AudioGraph>();
    std::map<std::string, AudioGraph::NodeId, std::less<>> node_ids;
    for (const auto& preset_node : prepared.preset.nodes) {
        if (node_ids.contains(preset_node.instance_id)) {
            return RigBuildError::InvalidTopology;
        }
        auto processor = registry.Instantiate(preset_node);
        if (std::holds_alternative<RigBuildError>(processor)) {
            return std::get<RigBuildError>(processor);
        }
        const auto node_id = graph->AddNode(std::move(std::get<std::unique_ptr<ModuleRuntimeProcessor>>(processor)));
        node_ids.emplace(preset_node.instance_id, node_id);
    }

    std::map<std::string, std::uint32_t, std::less<>> incoming_counts;
    std::map<std::string, std::uint32_t, std::less<>> outgoing_counts;
    for (const auto& preset_node : prepared.preset.nodes) {
        incoming_counts[preset_node.instance_id] = 0U;
        outgoing_counts[preset_node.instance_id] = 0U;
    }
    for (const auto& connection : prepared.preset.connections) {
        const auto source = node_ids.find(connection.source_instance_id);
        const auto destination = node_ids.find(connection.destination_instance_id);
        if (source == node_ids.end() || destination == node_ids.end() ||
            !graph->Connect(source->second, destination->second)) {
            return RigBuildError::InvalidTopology;
        }
        ++outgoing_counts[connection.source_instance_id];
        ++incoming_counts[connection.destination_instance_id];
    }
    for (const auto& preset_node : prepared.preset.nodes) {
        const auto node_id = node_ids.at(preset_node.instance_id);
        if (incoming_counts.at(preset_node.instance_id) == 0U && !graph->Connect(AudioGraph::InputNodeId, node_id)) {
            return RigBuildError::InvalidTopology;
        }
        if (outgoing_counts.at(preset_node.instance_id) == 0U && !graph->Connect(node_id, AudioGraph::OutputNodeId)) {
            return RigBuildError::InvalidTopology;
        }
    }
    if (!graph->Prepare(format).empty()) {
        return RigBuildError::GraphPreparationFailed;
    }
    auto result = prepared;
    result.runtime_node_indices.clear();
    for (std::size_t index = 0; index < result.preset.nodes.size(); ++index) {
        result.runtime_node_indices.emplace(result.preset.nodes[index].instance_id,
                                            static_cast<std::uint32_t>(index + 2U));
    }
    result.runtime_graph = std::move(graph);
    return result;
}

RigRuntime::RigRuntime(RealtimePresetMailbox& mailbox, AudioFormat format, PresetTransitionPolicy transition)
    : mailbox_(mailbox), format_(format), transition_policy_(transition), outgoing_buffer_(format.channel_count, format.max_block_size),
      incoming_buffer_(format.channel_count, format.max_block_size) {}

bool RigRuntime::Prepare() {
    prepared_ = format_.Validate().empty() && transition_policy_.Validate().has_value() == false &&
                outgoing_buffer_.IsValid() && incoming_buffer_.IsValid();
    transition_total_samples_ = transition_policy_.CrossfadeSamples(format_.sample_rate_hz);
    transition_position_ = 0U;
    current_ = nullptr;
    target_ = nullptr;
    return prepared_ && transition_total_samples_ > 0U;
}

void RigRuntime::Shutdown() noexcept {
    // setupProcessing is a host-stop boundary. No Process call may race this
    // routine, so the local target pointer is safe to retire before mailbox
    // atomics are reset.
    if (target_ != nullptr) {
        mailbox_.RetireAtControlPoint(target_);
    }
    if (current_ != nullptr) {
        mailbox_.RetireAtControlPoint(current_);
    }
    if (const auto* active = mailbox_.Active(); active != nullptr && active != current_ && active != target_) {
        mailbox_.RetireAtControlPoint(active);
    }
    if (const auto* pending = mailbox_.BeginPendingAtControlPoint(); pending != nullptr) {
        mailbox_.RetireAtControlPoint(pending);
    }
    mailbox_.ResetForReconfigure();
    current_ = nullptr;
    target_ = nullptr;
    transition_total_samples_ = 0U;
    transition_position_ = 0U;
    prepared_ = false;
}

void RigRuntime::ObservePending() noexcept {
    // Keep a queued replacement pending until the current crossfade has
    // finished. This gives one unambiguous target per transition and avoids
    // replacing a target halfway through an audio block.
    if (target_ != nullptr) {
        return;
    }
    const auto candidate = mailbox_.BeginPendingAtControlPoint();
    if (candidate == nullptr) {
        return;
    }
    if (candidate == current_) {
        // A duplicate submission can only occur through the legacy mailbox
        // API. Restore its claimed state instead of leaving Transition stuck.
        mailbox_.CommitActiveAtControlPoint(candidate);
        return;
    }
    if (candidate == target_) {
        return;
    }
    if (candidate->runtime_graph == nullptr && !candidate->preset.nodes.empty()) {
        // BeginPending has already claimed the slot. Reject it through the
        // same single-slot state transition; the old active graph remains
        // untouched and the controller may reclaim this slot later.
        mailbox_.RetireAtControlPoint(candidate);
        return;
    }
    if (current_ == nullptr) {
        current_ = candidate;
        mailbox_.CommitActiveAtControlPoint(current_);
        return;
    }
    target_ = candidate;
    transition_position_ = 0U;
}

void RigRuntime::FinishTransition() noexcept {
    current_ = target_;
    target_ = nullptr;
    transition_position_ = 0U;
    mailbox_.CommitActiveAtControlPoint(current_);
}

bool RigRuntime::Process(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept {
    if (!prepared_ || sample_count > format_.max_block_size || input.ChannelCount() != format_.channel_count ||
        output.ChannelCount() != format_.channel_count) {
        return false;
    }
    ObservePending();
    if (current_ == nullptr) {
        return output.CopyFrom(input, sample_count);
    }
    if (current_->runtime_graph == nullptr) {
        return output.CopyFrom(input, sample_count);
    }
    if (target_ == nullptr || target_->runtime_graph == nullptr) {
        return current_->runtime_graph->Process(input, output, sample_count);
    }
    if (!current_->runtime_graph->Process(input, outgoing_buffer_, sample_count) ||
        !target_->runtime_graph->Process(input, incoming_buffer_, sample_count)) {
        return false;
    }
    for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
        const auto progress = transition_total_samples_ <= 1U
            ? 1.0F
            : static_cast<float>(transition_position_ + sample) /
                  static_cast<float>(transition_total_samples_ - 1U);
        const auto gains = transition_policy_.GainsAt(progress);
        for (std::uint32_t channel = 0; channel < format_.channel_count; ++channel) {
            output.ChannelData(channel)[sample] = outgoing_buffer_.ChannelData(channel)[sample] * gains.outgoing +
                                                   incoming_buffer_.ChannelData(channel)[sample] * gains.incoming;
        }
    }
    transition_position_ += sample_count;
    if (transition_position_ >= transition_total_samples_) {
        FinishTransition();
    }
    return true;
}

bool RigRuntime::SetParameter(std::string_view instance_id, std::string_view parameter_id, float value) noexcept {
    auto apply = [instance_id, parameter_id, value](const PreparedPreset* prepared) noexcept {
        if (prepared == nullptr || prepared->runtime_graph == nullptr) {
            return false;
        }
        const auto location = prepared->runtime_node_indices.find(instance_id);
        if (location != prepared->runtime_node_indices.end()) {
            return prepared->runtime_graph->SetParameter(location->second, parameter_id, value);
        }
        return false;
    };

    bool applied = apply(current_);
    applied = apply(target_) || applied;
    if (const auto* pending = mailbox_.Pending(); pending != nullptr) {
        applied = apply(pending) || applied;
    }
    return applied;
}

bool RigRuntime::IsPrepared() const noexcept { return prepared_; }

std::uint32_t RigRuntime::TransitionSamples() const noexcept { return transition_total_samples_; }

bool RigRuntime::InTransition() const noexcept { return target_ != nullptr; }

} // namespace solo_rig
