#include "control/preset_controller.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <string_view>
#include <utility>

namespace solo_rig {

namespace {

PresetPreparationError ToPreparationError(PresetFileError error) {
    switch (error) {
    case PresetFileError::CannotRead:
        return PresetPreparationError::CannotRead;
    case PresetFileError::InvalidJson:
        return PresetPreparationError::InvalidJson;
    case PresetFileError::InvalidSchema:
    case PresetFileError::InvalidPreset:
    case PresetFileError::CannotWrite:
        return PresetPreparationError::InvalidSchema;
    }
    return PresetPreparationError::InvalidSchema;
}

PresetControlError ToControlError(PresetPreparationError error) {
    switch (error) {
    case PresetPreparationError::EmptyPresetId:
        return PresetControlError::EmptyPresetId;
    case PresetPreparationError::StableIdMismatch:
        return PresetControlError::StableIdMismatch;
    case PresetPreparationError::InvalidPreset:
    case PresetPreparationError::CannotRead:
    case PresetPreparationError::InvalidJson:
    case PresetPreparationError::InvalidSchema:
        return PresetControlError::InvalidPreset;
    }
    return PresetControlError::InvalidPreset;
}

struct ParameterTarget {
    std::string_view instance_id;
    std::string_view parameter_id;
};

std::optional<ParameterTarget> ParseParameterTarget(std::string_view target) {
    const auto separator = target.rfind('.');
    if (separator == std::string_view::npos || separator == 0U || separator + 1U == target.size()) {
        return std::nullopt;
    }
    return ParameterTarget{.instance_id = target.substr(0U, separator), .parameter_id = target.substr(separator + 1U)};
}

std::optional<PresetControlError> ApplyParameter(RigPreset& preset, const MidiAction& action) {
    const auto target = ParseParameterTarget(action.target_id);
    if (!target.has_value()) {
        return PresetControlError::InvalidParameterTarget;
    }
    const auto node = std::ranges::find_if(preset.nodes, [&target](const PresetNode& candidate) {
        return candidate.instance_id == target->instance_id;
    });
    if (node == preset.nodes.end() || !node->parameter_values.contains(target->parameter_id)) {
        return PresetControlError::UnknownParameterTarget;
    }
    node->parameter_values[std::string(target->parameter_id)] = action.normalized_value;
    return std::nullopt;
}

std::optional<std::string> ResolveSingleTarget(const std::vector<std::string>& targets) {
    if (targets.empty()) {
        return std::nullopt;
    }
    if (std::ranges::any_of(targets, [&targets](const std::string& target) { return target != targets.front(); })) {
        return std::string{};
    }
    return targets.front();
}

} // namespace

std::variant<PreparedPreset, PresetPreparationError> PresetPreparationService::Prepare(std::string preset_id,
                                                                                        RigPreset preset) const {
    if (preset_id.empty()) {
        return PresetPreparationError::EmptyPresetId;
    }
    if (!preset.preset_id.empty() && preset.preset_id != preset_id) {
        return PresetPreparationError::StableIdMismatch;
    }
    preset.preset_id = preset_id;
    if (!ValidatePreset(preset).empty()) {
        return PresetPreparationError::InvalidPreset;
    }
    return PreparedPreset{.preset_id = std::move(preset_id), .preset = std::move(preset)};
}

std::variant<PreparedPreset, PresetPreparationError> PresetPreparationService::PrepareFromFile(
    std::string preset_id,
    const std::filesystem::path& source) const {
    const auto loaded = LoadPreset(source);
    if (std::holds_alternative<PresetFileError>(loaded)) {
        return ToPreparationError(std::get<PresetFileError>(loaded));
    }
    return Prepare(std::move(preset_id), std::get<RigPreset>(loaded));
}

void RealtimePresetMailbox::Submit(const PreparedPreset& preset) noexcept {
    pending_.store(&preset, std::memory_order_release);
}

const PreparedPreset* RealtimePresetMailbox::ApplyPendingAtControlPoint() noexcept {
    const auto* pending = pending_.exchange(nullptr, std::memory_order_acq_rel);
    if (pending != nullptr) {
        active_.store(pending, std::memory_order_release);
    }
    return active_.load(std::memory_order_acquire);
}

const PreparedPreset* RealtimePresetMailbox::Active() const noexcept {
    return active_.load(std::memory_order_acquire);
}

bool RealtimePresetMailbox::IsLockFree() const noexcept {
    return pending_.is_lock_free() && active_.is_lock_free();
}

std::optional<PresetTransitionError> PresetTransitionPolicy::Validate() const noexcept {
    return std::isfinite(crossfade_milliseconds) && crossfade_milliseconds >= 1.0F && crossfade_milliseconds <= 100.0F
               ? std::nullopt
               : std::optional<PresetTransitionError>(PresetTransitionError::InvalidCrossfadeDuration);
}

std::uint32_t PresetTransitionPolicy::CrossfadeSamples(double sample_rate) const noexcept {
    if (Validate().has_value() || !std::isfinite(sample_rate) || sample_rate <= 0.0) {
        return 0U;
    }
    return static_cast<std::uint32_t>(std::lround(sample_rate * static_cast<double>(crossfade_milliseconds) / 1000.0));
}

PresetCrossfadeGains PresetTransitionPolicy::GainsAt(float progress) const noexcept {
    const auto clamped = std::clamp(progress, 0.0F, 1.0F);
    if (clamped == 0.0F) {
        return {.outgoing = 1.0F, .incoming = 0.0F};
    }
    if (clamped == 1.0F) {
        return {.outgoing = 0.0F, .incoming = 1.0F};
    }
    const auto angle = clamped * std::numbers::pi_v<float> * 0.5F;
    return {.outgoing = std::cos(angle), .incoming = std::sin(angle)};
}

std::optional<PresetControlError> PresetController::RegisterPreset(std::string preset_id, RigPreset preset) {
    const auto prepared = preparation_.Prepare(std::move(preset_id), std::move(preset));
    if (std::holds_alternative<PresetPreparationError>(prepared)) {
        return ToControlError(std::get<PresetPreparationError>(prepared));
    }
    return RegisterPrepared(std::get<PreparedPreset>(prepared));
}

std::optional<PresetControlError> PresetController::RegisterPresetFile(std::string preset_id,
                                                                        const std::filesystem::path& source) {
    const auto prepared = preparation_.PrepareFromFile(std::move(preset_id), source);
    if (std::holds_alternative<PresetPreparationError>(prepared)) {
        return ToControlError(std::get<PresetPreparationError>(prepared));
    }
    return RegisterPrepared(std::get<PreparedPreset>(prepared));
}

std::optional<PresetControlError> PresetController::RegisterPrepared(PreparedPreset prepared) {
    if (registered_presets_.contains(prepared.preset_id)) {
        return PresetControlError::DuplicatePresetId;
    }
    registered_presets_.emplace(prepared.preset_id, std::move(prepared.preset));
    return std::nullopt;
}

std::optional<PresetControlError> PresetController::ConfigureMidiBindings(std::vector<MidiBinding> bindings) {
    if (std::ranges::any_of(bindings, [](const MidiBinding& binding) { return !IsValidMidiBinding(binding); })) {
        return PresetControlError::InvalidMidiBinding;
    }
    midi_bindings_ = std::move(bindings);
    return std::nullopt;
}

std::optional<PresetControlError> PresetController::RegisterShortcut(ShortcutBinding binding) {
    const auto error = shortcuts_.Register(std::move(binding));
    if (!error.has_value()) {
        return std::nullopt;
    }
    return PresetControlError::InvalidShortcutBinding;
}

std::optional<PresetControlError> PresetController::QueuePresetActivation(std::string_view preset_id) {
    const auto preset = registered_presets_.find(preset_id);
    if (preset == registered_presets_.end()) {
        return PresetControlError::UnknownPreset;
    }
    const auto prepared = preparation_.Prepare(preset->first, preset->second);
    if (std::holds_alternative<PresetPreparationError>(prepared)) {
        return ToControlError(std::get<PresetPreparationError>(prepared));
    }
    static_cast<void>(StoreAndSubmit(std::get<PreparedPreset>(prepared)));
    return std::nullopt;
}

PresetControlDispatch PresetController::Dispatch(const std::vector<MidiMessage>& messages,
                                                 std::optional<KeyGesture> shortcut) {
    std::optional<std::string> shortcut_target;
    if (shortcut.has_value()) {
        const auto binding = shortcuts_.Resolve(*shortcut);
        if (binding.has_value()) {
            if (binding->action != ShortcutAction::LoadPreset) {
                return {.kind = PresetControlDispatchKind::Rejected,
                        .error = PresetControlError::UnsupportedShortcutAction};
            }
            shortcut_target = binding->target_id;
        }
    }

    std::vector<std::string> program_targets;
    std::vector<std::string> control_change_targets;
    std::vector<MidiAction> parameter_actions;
    for (const auto& message : messages) {
        for (const auto& binding : midi_bindings_) {
            const auto action = MapMidiMessage(binding, message);
            if (!action.has_value()) {
                continue;
            }
            if (action->target_kind == MidiTargetKind::Parameter) {
                parameter_actions.push_back(*action);
            } else if (message.type == MidiMessageType::ProgramChange) {
                program_targets.push_back(action->target_id);
            } else {
                control_change_targets.push_back(action->target_id);
            }
        }
    }

    const auto program_target = ResolveSingleTarget(program_targets);
    const auto control_change_target = ResolveSingleTarget(control_change_targets);
    if ((program_target.has_value() && program_target->empty()) ||
        (control_change_target.has_value() && control_change_target->empty())) {
        return {.kind = PresetControlDispatchKind::Rejected, .error = PresetControlError::AmbiguousPresetSelection};
    }

    std::optional<std::string> selected_target;
    if (shortcut_target.has_value()) {
        selected_target = *shortcut_target;
    } else if (program_target.has_value()) {
        selected_target = *program_target;
    } else if (control_change_target.has_value()) {
        selected_target = *control_change_target;
    }

    if (!selected_target.has_value() && parameter_actions.empty()) {
        return {};
    }
    if (selected_target.has_value() && !registered_presets_.contains(*selected_target)) {
        return {.kind = PresetControlDispatchKind::Rejected, .error = PresetControlError::UnknownPreset};
    }

    const PreparedPreset* base = nullptr;
    if (selected_target.has_value()) {
        const auto selected = registered_presets_.find(*selected_target);
        base = nullptr;
        RigPreset next_preset = selected->second;
        std::set<std::string, std::less<>> modified_parameters;
        for (const auto& action : parameter_actions) {
            if (!modified_parameters.insert(action.target_id).second) {
                return {.kind = PresetControlDispatchKind::Rejected, .error = PresetControlError::AmbiguousParameterUpdate};
            }
            if (const auto error = ApplyParameter(next_preset, action); error.has_value()) {
                return {.kind = PresetControlDispatchKind::Rejected, .error = *error};
            }
        }
        const auto prepared = preparation_.Prepare(*selected_target, std::move(next_preset));
        if (std::holds_alternative<PresetPreparationError>(prepared)) {
            return {.kind = PresetControlDispatchKind::Rejected,
                    .error = ToControlError(std::get<PresetPreparationError>(prepared))};
        }
        static_cast<void>(StoreAndSubmit(std::get<PreparedPreset>(prepared)));
        return {.kind = PresetControlDispatchKind::Queued, .preset_id = *selected_target};
    }

    base = staged_preset_ != nullptr ? staged_preset_ : mailbox_.Active();
    if (base == nullptr) {
        return {.kind = PresetControlDispatchKind::Rejected, .error = PresetControlError::NoActivePreset};
    }
    RigPreset next_preset = base->preset;
    std::set<std::string, std::less<>> modified_parameters;
    for (const auto& action : parameter_actions) {
        if (!modified_parameters.insert(action.target_id).second) {
            return {.kind = PresetControlDispatchKind::Rejected, .error = PresetControlError::AmbiguousParameterUpdate};
        }
        if (const auto error = ApplyParameter(next_preset, action); error.has_value()) {
            return {.kind = PresetControlDispatchKind::Rejected, .error = *error};
        }
    }
    const auto prepared = preparation_.Prepare(base->preset_id, std::move(next_preset));
    if (std::holds_alternative<PresetPreparationError>(prepared)) {
        return {.kind = PresetControlDispatchKind::Rejected,
                .error = ToControlError(std::get<PresetPreparationError>(prepared))};
    }
    static_cast<void>(StoreAndSubmit(std::get<PreparedPreset>(prepared)));
    return {.kind = PresetControlDispatchKind::Queued, .preset_id = base->preset_id};
}

const PreparedPreset* PresetController::ApplyPendingAtControlPoint() noexcept {
    return mailbox_.ApplyPendingAtControlPoint();
}

const PreparedPreset* PresetController::ActivePreset() const noexcept {
    return mailbox_.Active();
}

const PreparedPreset* PresetController::StoreAndSubmit(PreparedPreset prepared) {
    submitted_history_.push_back(std::make_unique<PreparedPreset>(std::move(prepared)));
    const auto* stored = submitted_history_.back().get();
    mailbox_.Submit(*stored);
    staged_preset_ = stored;
    return stored;
}

} // namespace solo_rig
