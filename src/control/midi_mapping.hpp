#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace solo_rig {

enum class MidiMessageType {
    ControlChange,
    ProgramChange,
};

struct MidiMessage {
    MidiMessageType type{MidiMessageType::ControlChange};
    std::uint8_t channel{};
    std::uint8_t number{};
    std::uint8_t value{};
};

enum class MidiTargetKind {
    Parameter,
    Preset,
};

struct MidiBinding {
    MidiMessageType message_type{MidiMessageType::ControlChange};
    std::uint8_t channel{};
    std::uint8_t number{};
    MidiTargetKind target_kind{MidiTargetKind::Parameter};
    std::string target_id;
    float minimum{};
    float maximum{1.0F};
};

struct MidiAction {
    MidiTargetKind target_kind{MidiTargetKind::Parameter};
    std::string target_id;
    float normalized_value{};
};

// Decodes the channel-voice messages SoloRig exposes for control. It keeps no
// dynamic storage so OS MIDI callbacks may feed bytes directly into it.
class MidiStreamDecoder {
public:
    [[nodiscard]] std::optional<MidiMessage> PushByte(std::uint8_t byte) noexcept;
    void Reset() noexcept;

private:
    std::uint8_t running_status_{};
    std::uint8_t first_data_byte_{};
    bool has_first_data_byte_{};
};

[[nodiscard]] bool IsValidMidiBinding(const MidiBinding& binding) noexcept;
[[nodiscard]] std::optional<MidiAction> MapMidiMessage(const MidiBinding& binding,
                                                        const MidiMessage& message);

} // namespace solo_rig
