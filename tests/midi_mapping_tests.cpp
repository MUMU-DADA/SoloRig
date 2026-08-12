#include "control/midi_mapping.hpp"
#include "control/midi_input.hpp"
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

TEST_CASE("MidiStreamDecoder decodes control changes and running status") {
    solo_rig::MidiStreamDecoder decoder;

    REQUIRE(!decoder.PushByte(0xB2U).has_value());
    REQUIRE(!decoder.PushByte(11U).has_value());
    const auto first = decoder.PushByte(64U);
    REQUIRE(first.has_value());
    REQUIRE(first->type == solo_rig::MidiMessageType::ControlChange);
    REQUIRE(first->channel == 2U);
    REQUIRE(first->number == 11U);
    REQUIRE(first->value == 64U);

    REQUIRE(!decoder.PushByte(12U).has_value());
    const auto second = decoder.PushByte(127U);
    REQUIRE(second.has_value());
    REQUIRE(second->channel == 2U);
    REQUIRE(second->number == 12U);
    REQUIRE(second->value == 127U);
}

TEST_CASE("MidiStreamDecoder decodes program changes and ignores realtime bytes") {
    solo_rig::MidiStreamDecoder decoder;

    REQUIRE(!decoder.PushByte(0xC1U).has_value());
    REQUIRE(!decoder.PushByte(0xF8U).has_value());
    const auto program = decoder.PushByte(5U);
    REQUIRE(program.has_value());
    REQUIRE(program->type == solo_rig::MidiMessageType::ProgramChange);
    REQUIRE(program->channel == 1U);
    REQUIRE(program->number == 5U);
    REQUIRE(program->value == 5U);
}

TEST_CASE("MidiStreamDecoder rejects data bytes without a channel status") {
    solo_rig::MidiStreamDecoder decoder;

    REQUIRE(!decoder.PushByte(42U).has_value());
    REQUIRE(!decoder.PushByte(0xF0U).has_value());
    REQUIRE(!decoder.PushByte(42U).has_value());
}

TEST_CASE("MidiMessageQueue preserves callback order without allocations") {
    solo_rig::MidiMessageQueue queue;
    REQUIRE(queue.TryPush({.type = solo_rig::MidiMessageType::ControlChange, .channel = 3, .number = 11, .value = 10}));
    REQUIRE(queue.TryPush({.type = solo_rig::MidiMessageType::ProgramChange, .channel = 3, .number = 4, .value = 4}));

    const auto first = queue.TryPop();
    const auto second = queue.TryPop();
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first->number == 11U);
    REQUIRE(second->type == solo_rig::MidiMessageType::ProgramChange);
    REQUIRE(!queue.TryPop().has_value());
}

TEST_CASE("WindowsMidiInput translates packed WinMM short messages without opening a device") {
    solo_rig::WindowsMidiInput input;
    input.OnPackedShortMessage(0x00400BB2U);
    const auto control_change = input.Poll();

    REQUIRE(control_change.has_value());
    REQUIRE(control_change->type == solo_rig::MidiMessageType::ControlChange);
    REQUIRE(control_change->channel == 2U);
    REQUIRE(control_change->number == 11U);
    REQUIRE(control_change->value == 64U);

    input.OnPackedShortMessage(0x000005C1U);
    const auto program_change = input.Poll();
    REQUIRE(program_change.has_value());
    REQUIRE(program_change->type == solo_rig::MidiMessageType::ProgramChange);
    REQUIRE(program_change->channel == 1U);
    REQUIRE(program_change->number == 5U);
}
