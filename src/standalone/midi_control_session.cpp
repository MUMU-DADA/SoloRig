#include "standalone/midi_control_session.hpp"

#include <algorithm>
#include <set>
#include <string_view>

namespace solo_rig::standalone {
namespace {

std::string ControlErrorText(PresetControlError error) {
    switch (error) {
    case PresetControlError::InvalidMidiBinding:
        return "invalid MIDI binding";
    case PresetControlError::UnknownParameterTarget:
        return "MIDI target parameter is missing";
    case PresetControlError::InvalidParameterTarget:
        return "MIDI parameter target is malformed";
    case PresetControlError::UnknownPreset:
        return "MIDI preset target is missing from the session bank";
    case PresetControlError::AmbiguousPresetSelection:
        return "MIDI preset selection is ambiguous";
    case PresetControlError::AmbiguousParameterUpdate:
        return "MIDI parameter update is ambiguous";
    case PresetControlError::NoActivePreset:
        return "MIDI control has no active preset";
    default:
        return "MIDI control request was rejected";
    }
}

std::string NarrowDeviceName(const std::wstring& name) {
    std::string result;
    result.reserve(name.size());
    for (const auto character : name) {
        result.push_back(character <= 0x7FU ? static_cast<char>(character) : '?');
    }
    return result;
}

bool SameBindingKey(const MidiBinding& left, const MidiBinding& right) noexcept {
    return left.message_type == right.message_type && left.channel == right.channel && left.number == right.number;
}

bool ParameterTargetExists(const RigPreset& preset, std::string_view target_id) {
    const auto separator = target_id.rfind('.');
    if (separator == std::string_view::npos || separator == 0U || separator + 1U == target_id.size()) {
        return false;
    }
    const auto node_id = target_id.substr(0U, separator);
    const auto parameter_id = target_id.substr(separator + 1U);
    const auto node = std::ranges::find_if(preset.nodes, [node_id](const PresetNode& candidate) {
        return candidate.instance_id == node_id;
    });
    return node != preset.nodes.end() && node->parameter_values.contains(parameter_id);
}

MidiLearnTarget RangeForParameter(const PresetNode& node, std::string parameter_id) {
    const auto target_id = node.instance_id + "." + parameter_id;
    MidiLearnTarget target{.target_id = target_id,
                           .display_name = target_id,
                           .minimum = 0.0F,
                           .maximum = 1.0F};
    if (node.module_id == "org.mumu-dada.solorig.drive") {
        if (parameter_id == "drive") {
            target.minimum = 0.0F;
            target.maximum = 16.0F;
        } else if (parameter_id == "outputGain") {
            target.minimum = 0.0F;
            target.maximum = 2.0F;
        } else if (parameter_id == "diodeThreshold") {
            target.minimum = 0.01F;
            target.maximum = 1.0F;
        }
    } else if (node.module_id == "org.mumu-dada.solorig.digital-delay") {
        if (parameter_id == "time") {
            target.minimum = 0.0F;
            target.maximum = 2.0F;
        } else if (parameter_id == "feedback") {
            target.minimum = -0.99F;
            target.maximum = 0.99F;
        } else if (parameter_id == "mix") {
            target.minimum = 0.0F;
            target.maximum = 1.0F;
        }
    }
    return target;
}

} // namespace

std::vector<MidiInputDevice> WindowsMidiInputSource::EnumerateDevices() {
    return WindowsMidiInput::EnumerateDevices();
}

bool WindowsMidiInputSource::Open(std::uint32_t device_index) {
    return input_.Open(device_index);
}

void WindowsMidiInputSource::Close() noexcept {
    input_.Close();
}

bool WindowsMidiInputSource::IsOpen() const noexcept {
    return input_.IsOpen();
}

std::optional<MidiMessage> WindowsMidiInputSource::Poll() noexcept {
    return input_.Poll();
}

std::uint64_t WindowsMidiInputSource::DroppedMessageCount() const noexcept {
    return input_.DroppedMessageCount();
}

StandaloneMidiControlSession::StandaloneMidiControlSession(RigEditorViewModel& editor,
                                                           std::unique_ptr<MidiInputSource> source)
    : editor_(editor), source_(std::move(source)), controller_(std::make_unique<PresetController>()) {
    static_cast<void>(SyncFromEditor());
}

StandaloneMidiControlSession::~StandaloneMidiControlSession() {
    Close();
}

std::vector<MidiInputDevice> StandaloneMidiControlSession::RefreshDevices() {
    devices_ = source_->EnumerateDevices();
    if (devices_.empty()) {
        learn_armed_ = false;
        learn_target_.clear();
        learn_minimum_ = 0.0F;
        learn_maximum_ = 1.0F;
        status_.state = MidiSessionState::NoDevice;
        status_.device_name.clear();
        status_.message = "MIDI input: no devices available";
        return devices_;
    }
    const auto selected = std::ranges::find_if(devices_, [this](const MidiInputDevice& device) {
        return device.index == status_.selected_device_index;
    });
    if (selected == devices_.end()) {
        status_.selected_device_index = devices_.front().index;
    }
    if (!source_->IsOpen()) {
        status_.state = MidiSessionState::Closed;
        status_.message = "MIDI input closed; devices refreshed";
    }
    return devices_;
}

bool StandaloneMidiControlSession::SelectDevice(std::uint32_t device_index) {
    const auto selected = std::ranges::find_if(devices_, [device_index](const MidiInputDevice& device) {
        return device.index == device_index;
    });
    if (selected == devices_.end()) {
        SetError("MIDI device selection is unavailable; refresh devices first");
        return false;
    }
    status_.selected_device_index = device_index;
    status_.device_name = selected->display_name;
    return true;
}

bool StandaloneMidiControlSession::OpenSelected() {
    return Open(status_.selected_device_index);
}

bool StandaloneMidiControlSession::Open(std::uint32_t device_index) {
    if (!SelectDevice(device_index)) {
        return false;
    }
    if (!source_->Open(device_index)) {
        source_->Close();
        learn_armed_ = false;
        learn_target_.clear();
        learn_minimum_ = 0.0F;
        learn_maximum_ = 1.0F;
        SetError("MIDI device could not be opened");
        return false;
    }
    observed_dropped_messages_ = source_->DroppedMessageCount();
    status_.state = MidiSessionState::Open;
    SetOpenMessage();
    return true;
}

void StandaloneMidiControlSession::Close() noexcept {
    if (source_ != nullptr) {
        source_->Close();
    }
    status_.state = MidiSessionState::Closed;
    learn_armed_ = false;
    learn_target_.clear();
    learn_minimum_ = 0.0F;
    learn_maximum_ = 1.0F;
    status_.device_name.clear();
    status_.message = "MIDI input closed";
}

bool StandaloneMidiControlSession::IsOpen() const noexcept {
    return source_ != nullptr && source_->IsOpen();
}

std::size_t StandaloneMidiControlSession::Drain() {
    if (!IsOpen()) {
        status_.message = "MIDI input closed; no control messages drained";
        return 0U;
    }
    std::vector<MidiMessage> messages;
    messages.reserve(MidiMessageQueue::Capacity);
    for (;;) {
        const auto message = source_->Poll();
        if (!message.has_value()) {
            break;
        }
        messages.push_back(*message);
    }
    status_.processed_messages = messages.size();
    const auto dropped = source_->DroppedMessageCount();
    status_.dropped_messages = dropped;
    const auto overflowed = dropped != observed_dropped_messages_;
    if (overflowed) {
        observed_dropped_messages_ = dropped;
        status_.message = "MIDI queue overflow; dropped messages";
        learn_armed_ = false;
        learn_target_.clear();
        learn_minimum_ = 0.0F;
        learn_maximum_ = 1.0F;
        status_.state = MidiSessionState::Error;
        return messages.size();
    }
    if (messages.empty()) {
        if (dropped == 0U) {
            SetOpenMessage();
        }
        return 0U;
    }
    std::optional<MidiMessage> learned_message;
    std::vector<MidiMessage> dispatch_messages;
    dispatch_messages.reserve(messages.size());
    for (const auto& message : messages) {
        if (learn_armed_ && !learned_message.has_value() &&
            message.type == MidiMessageType::ControlChange) {
            learned_message = message;
            continue;
        }
        dispatch_messages.push_back(message);
    }

    if (learned_message.has_value()) {
        const auto target = learn_target_;
        auto candidate = bindings_;
        const auto old_size = candidate.size();
        std::erase_if(candidate, [&learned_message](const MidiBinding& binding) {
            return binding.message_type == MidiMessageType::ControlChange &&
                   binding.channel == learned_message->channel && binding.number == learned_message->number;
        });
        const auto replaced = candidate.size() != old_size;
        candidate.push_back({.message_type = MidiMessageType::ControlChange,
                             .channel = learned_message->channel,
                             .number = learned_message->number,
                             .target_kind = MidiTargetKind::Parameter,
                             .target_id = target,
                             .minimum = learn_minimum_,
                             .maximum = learn_maximum_});
        if (!ConfigureBindings(std::move(candidate))) {
            learn_armed_ = false;
            learn_target_.clear();
            SetError("MIDI learn binding could not be applied");
            return messages.size();
        }
        learn_armed_ = false;
        learn_target_.clear();
        status_.state = MidiSessionState::Learned;
        status_.message = "learned CC " + std::to_string(learned_message->number) +
                          (replaced ? " (replaced existing mapping)" : "");
    }

    if (dispatch_messages.empty()) {
        return messages.size();
    }

    const auto dispatch = controller_->Dispatch(dispatch_messages);
    if (dispatch.kind == PresetControlDispatchKind::Rejected) {
        SetError(dispatch.error.has_value() ? ControlErrorText(*dispatch.error)
                                            : "MIDI control request was rejected");
        return messages.size();
    }
    if (dispatch.kind == PresetControlDispatchKind::Queued) {
        const auto* active = controller_->ApplyPendingAtControlPoint();
        if (active == nullptr) {
            SetError("MIDI control result was not prepared");
            return messages.size();
        }
        editor_.SetPreset(active->preset);
        // The controller owns bounded prepared slots. Rebuilding after the
        // editor receives a result retires that short-lived mailbox and keeps
        // repeated CC moves from exhausting slots without an audio consumer.
        if (!RebuildController()) {
            SetError("MIDI controller could not be refreshed");
            return messages.size();
        }
        status_.message = "MIDI control updated editor; ASIO backend not configured/available";
    } else {
        if (!overflowed) {
            SetOpenMessage();
        }
    }
    return messages.size();
}

bool StandaloneMidiControlSession::ConfigureBindings(std::vector<MidiBinding> bindings) {
    const auto current = editor_.ExportPreset();
    if (std::ranges::any_of(bindings, [](const MidiBinding& binding) {
            return !IsValidMidiBinding(binding);
        }) || std::ranges::any_of(bindings, [&current, this](const MidiBinding& binding) {
            if (binding.target_kind == MidiTargetKind::Parameter) {
                return !ParameterTargetExists(current, binding.target_id);
            }
            const auto current_id = current.preset_id.empty() ? std::string("standalone.current") : current.preset_id;
            return binding.target_id != current_id && !preset_bank_.contains(binding.target_id);
        }) || std::ranges::any_of(bindings, [&bindings](const MidiBinding& binding) {
            return std::ranges::count_if(bindings, [&binding](const MidiBinding& other) {
                       return SameBindingKey(binding, other);
                   }) > 1;
        })) {
        SetError("MIDI bindings are invalid or ambiguous");
        return false;
    }
    const auto previous = bindings_;
    bindings_ = std::move(bindings);
    for (auto& [unused_id, preset] : preset_bank_) {
        static_cast<void>(unused_id);
        preset.midi_bindings = bindings_;
    }
    if (!editor_.SetMidiBindings(bindings_)) {
        bindings_ = previous;
        for (auto& [unused_id, preset] : preset_bank_) {
            static_cast<void>(unused_id);
            preset.midi_bindings = previous;
        }
        SetError("MIDI bindings could not be applied to the editor");
        return false;
    }
    UpdateBindingCount();
    if (!RebuildController()) {
        bindings_ = previous;
        static_cast<void>(editor_.SetMidiBindings(previous));
        SetError("MIDI bindings could not be prepared");
        return false;
    }
    SetOpenMessage();
    return true;
}

bool StandaloneMidiControlSession::SyncFromEditor() {
    const auto preset = editor_.ExportPreset();
    bindings_ = preset.midi_bindings.value_or(std::vector<MidiBinding>{});
    learn_armed_ = false;
    learn_target_.clear();
    learn_minimum_ = 0.0F;
    learn_maximum_ = 1.0F;
    UpdateBindingCount();
    const auto rebuilt = RebuildController();
    if (rebuilt) {
        if (IsOpen()) {
            status_.state = MidiSessionState::Open;
            status_.message = "ready";
        } else if (status_.state != MidiSessionState::NoDevice) {
            status_.state = MidiSessionState::Closed;
            status_.message = "MIDI input closed";
        }
    }
    return rebuilt;
}

std::vector<MidiLearnTarget> StandaloneMidiControlSession::ParameterTargets() const {
    std::vector<MidiLearnTarget> targets;
    const auto preset = editor_.ExportPreset();
    for (const auto& node : preset.nodes) {
        for (const auto& [parameter_id, unused_value] : node.parameter_values) {
            static_cast<void>(unused_value);
            targets.push_back(RangeForParameter(node, parameter_id));
        }
    }
    return targets;
}

std::vector<MidiBinding> StandaloneMidiControlSession::Bindings() const {
    return bindings_;
}

bool StandaloneMidiControlSession::ArmLearn(std::string target_id) {
    const auto targets = ParameterTargets();
    const auto target = std::ranges::find_if(targets, [&target_id](const MidiLearnTarget& candidate) {
        return candidate.target_id == target_id;
    });
    if (target == targets.end()) {
        SetError("MIDI learn target is missing");
        return false;
    }
    if (!IsOpen()) {
        SetError("MIDI learn requires an open input");
        return false;
    }
    learn_target_ = std::move(target_id);
    learn_minimum_ = target->minimum;
    learn_maximum_ = target->maximum;
    learn_armed_ = true;
    SetLearnMessage("learn armed for " + learn_target_ + "; waiting for CC", MidiSessionState::LearnArmed);
    return true;
}

void StandaloneMidiControlSession::CancelLearn() noexcept {
    learn_armed_ = false;
    learn_target_.clear();
    learn_minimum_ = 0.0F;
    learn_maximum_ = 1.0F;
    SetOpenMessage();
}

bool StandaloneMidiControlSession::IsLearnArmed() const noexcept {
    return learn_armed_;
}

std::optional<std::string> StandaloneMidiControlSession::LearnTarget() const {
    return learn_armed_ ? std::optional<std::string>(learn_target_) : std::nullopt;
}

bool StandaloneMidiControlSession::RemoveBinding(std::size_t index) {
    if (index >= bindings_.size()) {
        SetError("MIDI mapping selection is invalid");
        return false;
    }
    auto candidate = bindings_;
    candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(index));
    if (!ConfigureBindings(std::move(candidate))) {
        return false;
    }
    status_.message = "MIDI binding removed";
    return true;
}

bool StandaloneMidiControlSession::ClearBindings() {
    if (!ConfigureBindings({})) {
        return false;
    }
    status_.message = "MIDI bindings cleared";
    return true;
}

bool StandaloneMidiControlSession::RegisterPresetBank(std::vector<std::pair<std::string, RigPreset>> presets) {
    std::map<std::string, RigPreset, std::less<>> candidate;
    for (auto& [id, preset] : presets) {
        if (id.empty() || candidate.contains(id) || !ValidatePreset(preset).empty()) {
            SetError("MIDI preset bank contains an invalid or duplicate preset");
            return false;
        }
        if (!preset.preset_id.empty() && preset.preset_id != id) {
            SetError("MIDI preset bank identifier does not match preset metadata");
            return false;
        }
        preset.preset_id = id;
        if (!preset.midi_bindings.has_value() && !bindings_.empty()) {
            preset.midi_bindings = bindings_;
        }
        candidate.emplace(id, std::move(preset));
    }
    const auto previous = std::move(preset_bank_);
    preset_bank_ = std::move(candidate);
    if (!RebuildController()) {
        preset_bank_ = std::move(previous);
        static_cast<void>(RebuildController());
        SetError("MIDI preset bank could not be prepared");
        return false;
    }
    SetOpenMessage();
    return true;
}

const MidiSessionStatus& StandaloneMidiControlSession::Status() const noexcept {
    return status_;
}

std::string StandaloneMidiControlSession::StatusText() const {
    std::string text = "MIDI: ";
    if (status_.state == MidiSessionState::Open) {
        text += "open";
        if (!status_.device_name.empty()) {
            text += " (" + NarrowDeviceName(status_.device_name) + ")";
        }
    } else if (status_.state == MidiSessionState::NoDevice) {
        text += "no device";
    } else if (status_.state == MidiSessionState::LearnArmed) {
        text += "learn armed";
    } else if (status_.state == MidiSessionState::Learned) {
        text += "learned";
    } else if (status_.state == MidiSessionState::Error) {
        text += "error";
    } else {
        text += "closed";
    }
    text += " | bindings: " + std::to_string(status_.binding_count);
    if (!status_.message.empty()) {
        text += " | " + status_.message;
    }
    return text;
}

const std::vector<MidiInputDevice>& StandaloneMidiControlSession::Devices() const noexcept {
    return devices_;
}

bool StandaloneMidiControlSession::RebuildController() {
    const auto current = editor_.ExportPreset();
    const auto current_id = current.preset_id.empty() ? std::string("standalone.current") : current.preset_id;
    RigPreset current_with_id = current;
    current_with_id.preset_id = current_id;
    if (!current_with_id.midi_bindings.has_value()) {
        current_with_id.midi_bindings = bindings_;
    }
    auto next = std::make_unique<PresetController>();
    for (const auto& [id, preset] : preset_bank_) {
        if (id == current_id) {
            continue;
        }
        if (next->RegisterPreset(id, preset).has_value()) {
            return false;
        }
    }
    if (next->RegisterPreset(current_id, current_with_id).has_value() ||
        next->ConfigureMidiBindings(bindings_).has_value() ||
        next->QueuePresetActivation(current_id).has_value() ||
        next->ApplyPendingAtControlPoint() == nullptr) {
        return false;
    }
    controller_ = std::move(next);
    return true;
}

void StandaloneMidiControlSession::SetError(std::string message) {
    learn_armed_ = false;
    learn_target_.clear();
    learn_minimum_ = 0.0F;
    learn_maximum_ = 1.0F;
    status_.state = MidiSessionState::Error;
    status_.message = std::move(message);
}

void StandaloneMidiControlSession::SetOpenMessage() {
    if (IsOpen()) {
        if (learn_armed_) {
            status_.state = MidiSessionState::LearnArmed;
        } else if (status_.state != MidiSessionState::Learned) {
            status_.state = MidiSessionState::Open;
            status_.message = "ready";
        }
    } else if (status_.state != MidiSessionState::NoDevice) {
        status_.state = MidiSessionState::Closed;
        status_.message = "MIDI input closed";
    }
}

void StandaloneMidiControlSession::SetLearnMessage(std::string message, MidiSessionState state) {
    status_.state = state;
    status_.message = std::move(message);
}

void StandaloneMidiControlSession::UpdateBindingCount() {
    status_.binding_count = bindings_.size();
}

} // namespace solo_rig::standalone
