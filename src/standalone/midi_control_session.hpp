#pragma once

#include "control/midi_input.hpp"
#include "control/preset_controller.hpp"
#include "ui/rig_editor.hpp"

#include <cstdint>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace solo_rig::standalone {

// OS MIDI access is kept behind this control-thread boundary. Tests provide a
// fake implementation; the real implementation is the existing WinMM input.
class MidiInputSource {
public:
    virtual ~MidiInputSource() = default;
    [[nodiscard]] virtual std::vector<MidiInputDevice> EnumerateDevices() = 0;
    [[nodiscard]] virtual bool Open(std::uint32_t device_index) = 0;
    virtual void Close() noexcept = 0;
    [[nodiscard]] virtual bool IsOpen() const noexcept = 0;
    [[nodiscard]] virtual std::optional<MidiMessage> Poll() noexcept = 0;
    [[nodiscard]] virtual std::uint64_t DroppedMessageCount() const noexcept = 0;
};

class WindowsMidiInputSource final : public MidiInputSource {
public:
    [[nodiscard]] std::vector<MidiInputDevice> EnumerateDevices() override;
    [[nodiscard]] bool Open(std::uint32_t device_index) override;
    void Close() noexcept override;
    [[nodiscard]] bool IsOpen() const noexcept override;
    [[nodiscard]] std::optional<MidiMessage> Poll() noexcept override;
    [[nodiscard]] std::uint64_t DroppedMessageCount() const noexcept override;

private:
    WindowsMidiInput input_;
};

enum class MidiSessionState {
    Closed,
    NoDevice,
    Open,
    LearnArmed,
    Learned,
    Error,
};

struct MidiLearnTarget {
    std::string target_id;
    std::string display_name;
    float minimum{};
    float maximum{1.0F};
};

struct MidiSessionStatus {
    MidiSessionState state{MidiSessionState::Closed};
    std::uint32_t selected_device_index{};
    std::wstring device_name;
    std::string message{"MIDI input closed"};
    std::size_t binding_count{};
    std::size_t processed_messages{};
    std::uint64_t dropped_messages{};
};

class StandaloneMidiControlSession final {
public:
    explicit StandaloneMidiControlSession(
        RigEditorViewModel& editor,
        std::unique_ptr<MidiInputSource> source = std::make_unique<WindowsMidiInputSource>());
    ~StandaloneMidiControlSession();

    StandaloneMidiControlSession(const StandaloneMidiControlSession&) = delete;
    StandaloneMidiControlSession& operator=(const StandaloneMidiControlSession&) = delete;

    // These methods are control-thread/window actions. Construction does not
    // enumerate or open hardware, so startup remains side-effect free.
    [[nodiscard]] std::vector<MidiInputDevice> RefreshDevices();
    [[nodiscard]] bool SelectDevice(std::uint32_t device_index);
    [[nodiscard]] bool OpenSelected();
    [[nodiscard]] bool Open(std::uint32_t device_index);
    void Close() noexcept;
    [[nodiscard]] bool IsOpen() const noexcept;

    // Drain is intentionally called outside any audio callback. It applies
    // prepared control results to the editor and never touches ASIO/VST3.
    [[nodiscard]] std::size_t Drain();
    [[nodiscard]] bool ConfigureBindings(std::vector<MidiBinding> bindings);
    [[nodiscard]] bool SyncFromEditor();
    [[nodiscard]] std::vector<MidiLearnTarget> ParameterTargets() const;
    [[nodiscard]] std::vector<MidiBinding> Bindings() const;
    [[nodiscard]] bool ArmLearn(std::string target_id);
    void CancelLearn() noexcept;
    [[nodiscard]] bool IsLearnArmed() const noexcept;
    [[nodiscard]] std::optional<std::string> LearnTarget() const;
    [[nodiscard]] bool RemoveBinding(std::size_t index);
    [[nodiscard]] bool ClearBindings();
    [[nodiscard]] bool RegisterPresetBank(
        std::vector<std::pair<std::string, RigPreset>> presets);

    [[nodiscard]] const MidiSessionStatus& Status() const noexcept;
    [[nodiscard]] std::string StatusText() const;
    [[nodiscard]] const std::vector<MidiInputDevice>& Devices() const noexcept;

private:
    [[nodiscard]] bool RebuildController();
    void SetError(std::string message);
    void SetOpenMessage();
    void UpdateBindingCount();
    void SetLearnMessage(std::string message, MidiSessionState state);

    RigEditorViewModel& editor_;
    std::unique_ptr<MidiInputSource> source_;
    std::unique_ptr<PresetController> controller_;
    std::vector<MidiInputDevice> devices_;
    std::map<std::string, RigPreset, std::less<>> preset_bank_;
    std::vector<MidiBinding> bindings_;
    bool learn_armed_{};
    std::string learn_target_;
    float learn_minimum_{};
    float learn_maximum_{1.0F};
    MidiSessionStatus status_;
    std::uint64_t observed_dropped_messages_{};
};

} // namespace solo_rig::standalone
