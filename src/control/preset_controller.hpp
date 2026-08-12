#pragma once

#include "content/preset.hpp"
#include "control/midi_mapping.hpp"
#include "control/shortcut_registry.hpp"

#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace solo_rig {

enum class PresetPreparationError {
    EmptyPresetId,
    InvalidPreset,
    StableIdMismatch,
    CannotRead,
    InvalidJson,
    InvalidSchema,
};

// This object is immutable after preparation. It may therefore be handed to
// the audio control point by pointer without parsing or allocating there.
struct PreparedPreset {
    std::string preset_id;
    RigPreset preset;
};

class PresetPreparationService {
public:
    // These functions run only on a non-real-time control or worker thread.
    [[nodiscard]] std::variant<PreparedPreset, PresetPreparationError> Prepare(std::string preset_id,
                                                                                RigPreset preset) const;
    [[nodiscard]] std::variant<PreparedPreset, PresetPreparationError> PrepareFromFile(
        std::string preset_id,
        const std::filesystem::path& source) const;
};

// A single producer submits immutable PreparedPreset objects; the audio
// control point consumes only atomics. Submitted objects must outlive the
// mailbox, so PresetController retains every object it submits until shutdown.
class RealtimePresetMailbox {
public:
    void Submit(const PreparedPreset& preset) noexcept;
    [[nodiscard]] const PreparedPreset* ApplyPendingAtControlPoint() noexcept;
    [[nodiscard]] const PreparedPreset* Active() const noexcept;
    [[nodiscard]] bool IsLockFree() const noexcept;

private:
    std::atomic<const PreparedPreset*> pending_{nullptr};
    std::atomic<const PreparedPreset*> active_{nullptr};
};

// The graph integration will render both prepared graphs for this short
// interval. This policy is already deterministic and testable, but the actual
// sample-by-sample crossfade belongs to the future audio-graph integration.
enum class PresetTransitionError {
    InvalidCrossfadeDuration,
};

struct PresetCrossfadeGains {
    float outgoing{};
    float incoming{};
};

struct PresetTransitionPolicy {
    float crossfade_milliseconds{10.0F};

    [[nodiscard]] std::optional<PresetTransitionError> Validate() const noexcept;
    [[nodiscard]] std::uint32_t CrossfadeSamples(double sample_rate) const noexcept;
    [[nodiscard]] PresetCrossfadeGains GainsAt(float progress) const noexcept;
};

enum class PresetControlError {
    EmptyPresetId,
    InvalidPreset,
    StableIdMismatch,
    DuplicatePresetId,
    UnknownPreset,
    InvalidMidiBinding,
    InvalidShortcutBinding,
    UnsupportedShortcutAction,
    NoActivePreset,
    AmbiguousPresetSelection,
    InvalidParameterTarget,
    UnknownParameterTarget,
    AmbiguousParameterUpdate,
};

enum class PresetControlDispatchKind {
    Ignored,
    Queued,
    Rejected,
};

struct PresetControlDispatch {
    PresetControlDispatchKind kind{PresetControlDispatchKind::Ignored};
    std::optional<PresetControlError> error;
    std::string preset_id;
};

// The controller runs outside the audio callback. It deterministically
// resolves input, validates a complete next state, prepares an immutable copy,
// then submits that copy to RealtimePresetMailbox for the audio control point.
class PresetController {
public:
    [[nodiscard]] std::optional<PresetControlError> RegisterPreset(std::string preset_id, RigPreset preset);
    [[nodiscard]] std::optional<PresetControlError> RegisterPresetFile(std::string preset_id,
                                                                        const std::filesystem::path& source);
    [[nodiscard]] std::optional<PresetControlError> ConfigureMidiBindings(std::vector<MidiBinding> bindings);
    [[nodiscard]] std::optional<PresetControlError> RegisterShortcut(ShortcutBinding binding);
    [[nodiscard]] std::optional<PresetControlError> QueuePresetActivation(std::string_view preset_id);

    // Priority is: LoadPreset shortcut, Program Change preset target, Control
    // Change preset target. Parameter actions then apply to the selected
    // preset. Conflicting targets at one priority reject the whole batch.
    [[nodiscard]] PresetControlDispatch Dispatch(const std::vector<MidiMessage>& messages,
                                                 std::optional<KeyGesture> shortcut = std::nullopt);

    [[nodiscard]] const PreparedPreset* ApplyPendingAtControlPoint() noexcept;
    [[nodiscard]] const PreparedPreset* ActivePreset() const noexcept;

private:
    [[nodiscard]] std::optional<PresetControlError> RegisterPrepared(PreparedPreset prepared);
    [[nodiscard]] const PreparedPreset* StoreAndSubmit(PreparedPreset prepared);

    PresetPreparationService preparation_;
    RealtimePresetMailbox mailbox_;
    std::map<std::string, RigPreset, std::less<>> registered_presets_;
    std::vector<MidiBinding> midi_bindings_;
    ShortcutRegistry shortcuts_;
    std::vector<std::unique_ptr<PreparedPreset>> submitted_history_;
    const PreparedPreset* staged_preset_{};
};

} // namespace solo_rig
