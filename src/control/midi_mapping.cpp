#include "control/midi_mapping.hpp"

#include <cmath>

namespace solo_rig {

std::optional<MidiMessage> MidiStreamDecoder::PushByte(std::uint8_t byte) noexcept {
    if ((byte & 0x80U) != 0U) {
        // MIDI realtime messages may occur anywhere in a running-status stream.
        if (byte >= 0xF8U) {
            return std::nullopt;
        }
        const auto message_type = static_cast<std::uint8_t>(byte & 0xF0U);
        if (message_type == 0xB0U || message_type == 0xC0U) {
            running_status_ = byte;
            has_first_data_byte_ = false;
            return std::nullopt;
        }
        Reset();
        return std::nullopt;
    }

    if (running_status_ == 0U) {
        return std::nullopt;
    }

    const auto message_type = static_cast<std::uint8_t>(running_status_ & 0xF0U);
    const auto channel = static_cast<std::uint8_t>(running_status_ & 0x0FU);
    if (message_type == 0xC0U) {
        return MidiMessage{.type = MidiMessageType::ProgramChange, .channel = channel, .number = byte, .value = byte};
    }

    if (!has_first_data_byte_) {
        first_data_byte_ = byte;
        has_first_data_byte_ = true;
        return std::nullopt;
    }

    has_first_data_byte_ = false;
    return MidiMessage{.type = MidiMessageType::ControlChange,
                       .channel = channel,
                       .number = first_data_byte_,
                       .value = byte};
}

void MidiStreamDecoder::Reset() noexcept {
    running_status_ = 0U;
    first_data_byte_ = 0U;
    has_first_data_byte_ = false;
}

bool IsValidMidiBinding(const MidiBinding& binding) noexcept {
    return binding.channel < 16U && binding.number < 128U && !binding.target_id.empty() &&
           (binding.message_type == MidiMessageType::ControlChange || binding.message_type == MidiMessageType::ProgramChange) &&
           (binding.target_kind == MidiTargetKind::Parameter || binding.target_kind == MidiTargetKind::Preset) &&
           std::isfinite(binding.minimum) && std::isfinite(binding.maximum) && binding.minimum <= binding.maximum;
}

std::optional<MidiAction> MapMidiMessage(const MidiBinding& binding, const MidiMessage& message) {
    if (!IsValidMidiBinding(binding) || message.channel >= 16U || message.number >= 128U ||
        binding.message_type != message.type || binding.channel != message.channel || binding.number != message.number) {
        return std::nullopt;
    }
    const float unit_value = static_cast<float>(message.value) / 127.0F;
    return MidiAction{
        .target_kind = binding.target_kind,
        .target_id = binding.target_id,
        .normalized_value = binding.minimum + (binding.maximum - binding.minimum) * unit_value,
    };
}

} // namespace solo_rig
