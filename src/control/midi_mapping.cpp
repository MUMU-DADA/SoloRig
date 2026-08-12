#include "control/midi_mapping.hpp"

#include <cmath>

namespace solo_rig {

bool IsValidMidiBinding(const MidiBinding& binding) noexcept {
    return binding.channel < 16U && binding.number < 128U && !binding.target_id.empty() &&
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
