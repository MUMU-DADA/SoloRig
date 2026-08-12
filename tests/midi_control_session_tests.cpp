#include "standalone/midi_control_session.hpp"
#include "test_framework.hpp"

#include <memory>

namespace {

class FakeMidiInputSource final : public solo_rig::standalone::MidiInputSource {
public:
    std::vector<solo_rig::MidiInputDevice> EnumerateDevices() override {
        ++enumerate_count;
        return devices;
    }

    bool Open(std::uint32_t device_index) override {
        ++open_count;
        if (!open_result || device_index != devices.front().index) {
            return false;
        }
        open = true;
        return true;
    }

    void Close() noexcept override {
        ++close_count;
        open = false;
    }

    bool IsOpen() const noexcept override {
        return open;
    }

    std::optional<solo_rig::MidiMessage> Poll() noexcept override {
        return queue.TryPop();
    }

    std::uint64_t DroppedMessageCount() const noexcept override {
        return dropped;
    }

    void Inject(const solo_rig::MidiMessage& message) noexcept {
        if (!queue.TryPush(message)) {
            ++dropped;
        }
    }

    std::vector<solo_rig::MidiInputDevice> devices{{.index = 4U, .display_name = L"Fake Keyboard"}};
    solo_rig::MidiMessageQueue queue;
    bool open_result{true};
    bool open{};
    std::size_t enumerate_count{};
    std::size_t open_count{};
    std::size_t close_count{};
    std::uint64_t dropped{};
};

solo_rig::MidiBinding ParameterBinding() {
    return {.message_type = solo_rig::MidiMessageType::ControlChange,
            .channel = 0,
            .number = 11,
            .target_kind = solo_rig::MidiTargetKind::Parameter,
            .target_id = "drive-1.drive",
            .minimum = 0.0F,
            .maximum = 1.0F};
}

} // namespace

TEST_CASE("Standalone MIDI session starts closed without enumerating hardware and applies CC to editor") {
    solo_rig::RigEditorViewModel editor;
    auto fake = std::make_unique<FakeMidiInputSource>();
    auto* source = fake.get();
    solo_rig::standalone::StandaloneMidiControlSession session(editor, std::move(fake));

    REQUIRE(source->enumerate_count == 0U);
    REQUIRE(source->open_count == 0U);
    REQUIRE(!session.IsOpen());
    REQUIRE(session.Status().state == solo_rig::standalone::MidiSessionState::Closed);
    REQUIRE(session.StatusText().find("MIDI: closed") != std::string::npos);
    REQUIRE(session.ConfigureBindings({ParameterBinding()}));
    REQUIRE(session.RefreshDevices().size() == 1U);
    REQUIRE(session.OpenSelected());

    const auto before = editor.ExportPreset().nodes.front().parameter_values.at("drive");
    source->Inject({.type = solo_rig::MidiMessageType::ControlChange,
                    .channel = 0,
                    .number = 11,
                    .value = 127});
    REQUIRE(session.Drain() == 1U);
    REQUIRE(editor.ExportPreset().nodes.front().parameter_values.at("drive") == 1.0F);
    REQUIRE(editor.ExportPreset().nodes.front().parameter_values.at("drive") != before);
    REQUIRE(editor.IsDirty());
    REQUIRE(session.StatusText().find("updated editor") != std::string::npos);
    REQUIRE(session.StatusText().find("ASIO backend not configured/available") != std::string::npos);
    session.Close();
    REQUIRE(!session.IsOpen());
    REQUIRE(source->close_count > 0U);
}

TEST_CASE("Standalone MIDI session rejects invalid targets and reports queue overflow without changing the editor") {
    solo_rig::RigEditorViewModel editor;
    auto fake = std::make_unique<FakeMidiInputSource>();
    auto* source = fake.get();
    solo_rig::standalone::StandaloneMidiControlSession session(editor, std::move(fake));
    REQUIRE(!session.ConfigureBindings({{.channel = 0,
                                         .number = 11,
                                         .target_kind = solo_rig::MidiTargetKind::Parameter,
                                         .target_id = "missing.drive",
                                         .minimum = 0.0F,
                                         .maximum = 1.0F}}));
    REQUIRE(!editor.MidiBindings().has_value());
    REQUIRE(session.ConfigureBindings({ParameterBinding()}));
    REQUIRE(session.RefreshDevices().size() == 1U);
    REQUIRE(session.OpenSelected());
    const auto before = editor.ExportPreset().nodes.front().parameter_values.at("drive");
    source->Inject({.type = solo_rig::MidiMessageType::ControlChange,
                    .channel = 1,
                    .number = 11,
                    .value = 127});
    REQUIRE(session.Drain() == 1U);
    REQUIRE(editor.ExportPreset().nodes.front().parameter_values.at("drive") == before);
    REQUIRE(session.Status().state == solo_rig::standalone::MidiSessionState::Open);

    for (std::size_t index = 0; index < solo_rig::MidiMessageQueue::Capacity + 8U; ++index) {
        source->Inject({.type = solo_rig::MidiMessageType::ControlChange,
                        .channel = 1,
                        .number = 11,
                        .value = 127});
    }
    static_cast<void>(session.Drain());
    REQUIRE(session.Status().dropped_messages > 0U);
    REQUIRE(session.StatusText().find("queue overflow") != std::string::npos);
}

TEST_CASE("Standalone MIDI session uses a deterministic preset bank for Program Change") {
    solo_rig::RigEditorViewModel editor;
    auto fake = std::make_unique<FakeMidiInputSource>();
    auto* source = fake.get();
    solo_rig::standalone::StandaloneMidiControlSession session(editor, std::move(fake));
    auto lead = solo_rig::RigEditorViewModel::DefaultEditorPreset();
    lead.preset_id = "lead";
    lead.name = "Lead";
    lead.nodes.front().parameter_values.at("drive") = 9.0F;
    REQUIRE(session.RegisterPresetBank({{"lead", lead}}));
    REQUIRE(session.ConfigureBindings({{.message_type = solo_rig::MidiMessageType::ProgramChange,
                                        .channel = 0,
                                        .number = 5,
                                        .target_kind = solo_rig::MidiTargetKind::Preset,
                                        .target_id = "lead",
                                        .minimum = 0.0F,
                                        .maximum = 1.0F}}));
    REQUIRE(session.RefreshDevices().size() == 1U);
    REQUIRE(session.OpenSelected());
    source->Inject({.type = solo_rig::MidiMessageType::ProgramChange,
                    .channel = 0,
                    .number = 5,
                    .value = 5});
    REQUIRE(session.Drain() == 1U);
    REQUIRE(editor.ExportPreset().preset_id == "lead");
    REQUIRE(editor.ExportPreset().nodes.front().parameter_values.at("drive") == 9.0F);
}

TEST_CASE("Standalone MIDI session reports open failure, no-device state and closes fake input safely") {
    solo_rig::RigEditorViewModel editor;
    auto fake = std::make_unique<FakeMidiInputSource>();
    auto* source = fake.get();
    source->open_result = false;
    solo_rig::standalone::StandaloneMidiControlSession session(editor, std::move(fake));
    REQUIRE(session.RefreshDevices().size() == 1U);
    REQUIRE(!session.OpenSelected());
    REQUIRE(session.Status().state == solo_rig::standalone::MidiSessionState::Error);
    source->devices.clear();
    REQUIRE(session.RefreshDevices().empty());
    REQUIRE(session.Status().state == solo_rig::standalone::MidiSessionState::NoDevice);
    session.Close();
    REQUIRE(source->close_count > 0U);
}

TEST_CASE("Standalone MIDI learn captures only the next CC and replaces conflicting ownership") {
    solo_rig::RigEditorViewModel editor;
    auto fake = std::make_unique<FakeMidiInputSource>();
    auto* source = fake.get();
    solo_rig::standalone::StandaloneMidiControlSession session(editor, std::move(fake));
    REQUIRE(session.ParameterTargets().size() >= 2U);
    REQUIRE(session.RefreshDevices().size() == 1U);
    REQUIRE(session.OpenSelected());
    REQUIRE(session.ArmLearn("delay-1.time"));
    REQUIRE(session.IsLearnArmed());
    REQUIRE(session.Status().state == solo_rig::standalone::MidiSessionState::LearnArmed);

    const auto before_learn = editor.ExportPreset().nodes.at(1).parameter_values.at("time");
    source->Inject({.type = solo_rig::MidiMessageType::ProgramChange,
                    .channel = 0,
                    .number = 7,
                    .value = 7});
    source->Inject({.type = solo_rig::MidiMessageType::ControlChange,
                    .channel = 0,
                    .number = 74,
                    .value = 127});
    REQUIRE(session.Drain() == 2U);
    REQUIRE(!session.IsLearnArmed());
    REQUIRE(editor.ExportPreset().nodes.at(1).parameter_values.at("time") == before_learn);
    REQUIRE(session.Status().state == solo_rig::standalone::MidiSessionState::Learned);
    REQUIRE(session.StatusText().find("learned") != std::string::npos);
    const auto learned = session.Bindings();
    REQUIRE(learned.size() == 1U);
    REQUIRE(learned.front().target_id == "delay-1.time");
    REQUIRE(learned.front().number == 74U);
    REQUIRE(learned.front().minimum == 0.0F);
    REQUIRE(learned.front().maximum == 2.0F);
    REQUIRE(editor.IsDirty());

    source->Inject({.type = solo_rig::MidiMessageType::ControlChange,
                    .channel = 0,
                    .number = 74,
                    .value = 64});
    REQUIRE(session.Drain() == 1U);
    REQUIRE(editor.ExportPreset().nodes.at(1).parameter_values.at("time") > 0.99F);
    REQUIRE(editor.ExportPreset().nodes.at(1).parameter_values.at("time") < 1.02F);

    REQUIRE(session.ClearBindings());
    REQUIRE(session.ArmLearn("drive-1.drive"));
    source->Inject({.type = solo_rig::MidiMessageType::ControlChange,
                    .channel = 0,
                    .number = 76,
                    .value = 127});
    REQUIRE(session.Drain() == 1U);
    REQUIRE(session.Bindings().front().minimum == 0.0F);
    REQUIRE(session.Bindings().front().maximum == 16.0F);
    REQUIRE(editor.ExportPreset().nodes.front().parameter_values.at("drive") == 4.0F);
    source->Inject({.type = solo_rig::MidiMessageType::ControlChange,
                    .channel = 0,
                    .number = 76,
                    .value = 64});
    REQUIRE(session.Drain() == 1U);
    REQUIRE(editor.ExportPreset().nodes.front().parameter_values.at("drive") > 7.9F);
    REQUIRE(editor.ExportPreset().nodes.front().parameter_values.at("drive") < 8.1F);
    source->Inject({.type = solo_rig::MidiMessageType::ControlChange,
                    .channel = 0,
                    .number = 76,
                    .value = 127});
    REQUIRE(session.Drain() == 1U);
    REQUIRE(editor.ExportPreset().nodes.front().parameter_values.at("drive") == 16.0F);

    REQUIRE(session.ConfigureBindings({{.message_type = solo_rig::MidiMessageType::ControlChange,
                                        .channel = 0,
                                        .number = 74,
                                        .target_kind = solo_rig::MidiTargetKind::Parameter,
                                        .target_id = "delay-1.time",
                                        .minimum = 0.0F,
                                        .maximum = 2.0F}}));
    REQUIRE(session.ArmLearn("drive-1.drive"));
    source->Inject({.type = solo_rig::MidiMessageType::ControlChange,
                    .channel = 0,
                    .number = 74,
                    .value = 32});
    REQUIRE(session.Drain() == 1U);
    REQUIRE(session.Bindings().size() == 1U);
    REQUIRE(session.Bindings().front().target_id == "drive-1.drive");
    REQUIRE(session.StatusText().find("replaced") != std::string::npos);
}

TEST_CASE("Standalone MIDI learn cancellation, close and binding management are safe") {
    solo_rig::RigEditorViewModel editor;
    auto fake = std::make_unique<FakeMidiInputSource>();
    auto* source = fake.get();
    solo_rig::standalone::StandaloneMidiControlSession session(editor, std::move(fake));
    REQUIRE(!session.ArmLearn("missing.parameter"));
    REQUIRE(!session.IsLearnArmed());
    REQUIRE(!session.ArmLearn("drive-1.drive"));
    REQUIRE(session.StatusText().find("requires an open input") != std::string::npos);
    REQUIRE(session.RefreshDevices().size() == 1U);
    REQUIRE(session.OpenSelected());
    REQUIRE(!session.ArmLearn("missing.parameter"));
    REQUIRE(session.ArmLearn("drive-1.drive"));
    session.CancelLearn();
    REQUIRE(!session.IsLearnArmed());
    REQUIRE(session.ConfigureBindings({ParameterBinding()}));
    REQUIRE(session.RemoveBinding(0U));
    REQUIRE(session.Bindings().empty());
    REQUIRE(session.ClearBindings());
    REQUIRE(session.Bindings().empty());
    REQUIRE(session.ArmLearn("drive-1.drive"));
    session.Close();
    REQUIRE(!session.IsLearnArmed());
    REQUIRE(!source->open);
}
