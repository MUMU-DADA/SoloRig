#include "control/midi_mapping.hpp"
#include "test_framework.hpp"

TEST_CASE("MIDI CC binding maps the full 7-bit range to a configured parameter range") {
    const solo_rig::MidiBinding binding{
        .message_type = solo_rig::MidiMessageType::ControlChange,
        .channel = 2,
        .number = 11,
        .target_kind = solo_rig::MidiTargetKind::Parameter,
        .target_id = "drive-1.drive",
        .minimum = 0.2F,
        .maximum = 0.8F,
    };
    const auto action = solo_rig::MapMidiMessage(binding, {.type = solo_rig::MidiMessageType::ControlChange,
                                                           .channel = 2,
                                                           .number = 11,
                                                           .value = 127});

    REQUIRE(action.has_value());
    REQUIRE(action->normalized_value == 0.8F);
}

TEST_CASE("MIDI mapping ignores a different channel or message type") {
    const solo_rig::MidiBinding binding{.channel = 1, .number = 7, .target_id = "level"};

    REQUIRE(!solo_rig::MapMidiMessage(binding, {.channel = 2, .number = 7, .value = 127}).has_value());
    REQUIRE(!solo_rig::MapMidiMessage(binding, {.type = solo_rig::MidiMessageType::ProgramChange,
                                                 .channel = 1,
                                                 .number = 7,
                                                 .value = 1})
                 .has_value());
}
