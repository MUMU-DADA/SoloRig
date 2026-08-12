#pragma once

#include "content/preset.hpp"
#include "control/midi_mapping.hpp"
#include "control/shortcut_registry.hpp"
#include "core/audio_format.hpp"

#include <atomic>
#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace solo_rig {

class AudioGraph;
class RigModuleRegistry;

enum class PreparedSlotState : std::uint8_t {
    Detached,
    Free,
    Published,
    Pending,
    Active,
    Transition,
    Retired,
};

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
    // Set only by the non-real-time RigRuntime builder. The graph structure
    // is immutable after preparation; its preallocated DSP state is mutable
    // only from the audio thread.
    std::shared_ptr<AudioGraph> runtime_graph;
    // Built once by RigRuntimeBuilder. The audio thread uses this immutable
    // index to locate a module without scanning or allocating.
    std::map<std::string, std::uint32_t, std::less<>> runtime_node_indices;
    // Points to the owning controller slot's single ownership state. It is
    // never dereferenced by the real-time path except for atomic state CAS.
    std::atomic<PreparedSlotState>* slot_state{};
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
    [[nodiscard]] bool Publish(const PreparedPreset& preset) noexcept;
    [[nodiscard]] const PreparedPreset* BeginPendingAtControlPoint() noexcept;
    void CommitActiveAtControlPoint(const PreparedPreset* preset) noexcept;
    void RetireAtControlPoint(const PreparedPreset* preset) noexcept;
    [[nodiscard]] const PreparedPreset* ApplyPendingAtControlPoint() noexcept;
    [[nodiscard]] const PreparedPreset* Active() const noexcept;
    [[nodiscard]] const PreparedPreset* Pending() const noexcept;
    // Called only while the host has stopped processing, before replacing a
    // plugin's prepared runtime during setupProcessing. Any pointed-to slot
    // is retired before the atomics are cleared.
    void ResetForReconfigure() noexcept;
    [[nodiscard]] bool IsLockFree() const noexcept;

private:
    std::atomic<const PreparedPreset*> pending_{nullptr};
    std::atomic<const PreparedPreset*> active_{nullptr};
};

// RigRuntime renders both prepared graphs for this short interval using these
// deterministic sample-level equal-power gains.
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
    RuntimeBuildFailed,
    RuntimeStorageExhausted,
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
    explicit PresetController(RigModuleRegistry* registry = nullptr,
                               AudioFormat format = {.sample_rate_hz = 192000.0, .max_block_size = 1024, .channel_count = 2});
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
    [[nodiscard]] RealtimePresetMailbox& MailboxForControl() noexcept;
    [[nodiscard]] std::size_t SubmittedSlotCapacity() const noexcept;

private:
    struct PreparedSlot {
        std::unique_ptr<PreparedPreset> preset;
        std::atomic<PreparedSlotState> state{PreparedSlotState::Free};
    };

    [[nodiscard]] std::optional<PresetControlError> RegisterPrepared(PreparedPreset prepared);
    [[nodiscard]] std::optional<PresetControlError> StoreAndSubmit(PreparedPreset prepared);

    PresetPreparationService preparation_;
    RealtimePresetMailbox mailbox_;
    std::map<std::string, RigPreset, std::less<>> registered_presets_;
    std::vector<MidiBinding> midi_bindings_;
    ShortcutRegistry shortcuts_;
    std::array<PreparedSlot, 8> submitted_slots_;
    std::size_t next_slot_{};
    const PreparedPreset* staged_preset_{};
    RigModuleRegistry* runtime_registry_{};
    AudioFormat runtime_format_{};
};

} // namespace solo_rig
