#include "content/preset.hpp"
#include "control/preset_controller.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

solo_rig::RigPreset MakePreset(std::string name, float drive) {
    return {
        .name = std::move(name),
        .nodes = {{.instance_id = "drive-1",
                   .module_id = "org.mumu-dada.solorig.drive",
                   .layout_x = 10.0,
                   .layout_y = 20.0,
                   .parameter_values = {{"drive", drive}}}},
    };
}

} // namespace

TEST_CASE("Preset preparation assigns a stable ID and rejects incompatible IDs") {
    solo_rig::PresetPreparationService preparation;
    auto preset = MakePreset("Lead", 0.8F);
    preset.preset_id = "another-rig";

    const auto rejected = preparation.Prepare("lead", preset);
    const auto prepared = preparation.Prepare("lead", MakePreset("Lead", 0.8F));

    REQUIRE(std::holds_alternative<solo_rig::PresetPreparationError>(rejected));
    REQUIRE(std::get<solo_rig::PresetPreparationError>(rejected) ==
            solo_rig::PresetPreparationError::StableIdMismatch);
    REQUIRE(std::holds_alternative<solo_rig::PreparedPreset>(prepared));
    REQUIRE(std::get<solo_rig::PreparedPreset>(prepared).preset.preset_id == "lead");
}

TEST_CASE("RealtimePresetMailbox only applies fully prepared data at its lock-free control point") {
    solo_rig::PresetPreparationService preparation;
    const auto clean = preparation.Prepare("clean", MakePreset("Clean", 0.2F));
    const auto lead = preparation.Prepare("lead", MakePreset("Lead", 0.8F));
    solo_rig::RealtimePresetMailbox mailbox;

    REQUIRE(std::holds_alternative<solo_rig::PreparedPreset>(clean));
    REQUIRE(std::holds_alternative<solo_rig::PreparedPreset>(lead));
    REQUIRE(mailbox.IsLockFree());
    mailbox.Submit(std::get<solo_rig::PreparedPreset>(clean));
    REQUIRE(mailbox.Active() == nullptr);
    REQUIRE(mailbox.ApplyPendingAtControlPoint()->preset_id == "clean");
    mailbox.Submit(std::get<solo_rig::PreparedPreset>(lead));
    REQUIRE(mailbox.Active()->preset_id == "clean");
    REQUIRE(mailbox.ApplyPendingAtControlPoint()->preset_id == "lead");
}

TEST_CASE("PresetController applies shortcut then Program Change then CC by defined priority") {
    solo_rig::PresetController controller;
    REQUIRE(!controller.RegisterPreset("clean", MakePreset("Clean", 0.2F)).has_value());
    REQUIRE(!controller.RegisterPreset("lead", MakePreset("Lead", 0.5F)).has_value());
    REQUIRE(!controller.RegisterPreset("solo", MakePreset("Solo", 0.7F)).has_value());
    REQUIRE(!controller.QueuePresetActivation("clean").has_value());
    REQUIRE(controller.ApplyPendingAtControlPoint()->preset_id == "clean");
    REQUIRE(!controller.RegisterShortcut({.gesture = {.key = "F2"},
                                          .action = solo_rig::ShortcutAction::LoadPreset,
                                          .target_id = "solo"})
                 .has_value());
    REQUIRE(!controller.ConfigureMidiBindings({
                 {.message_type = solo_rig::MidiMessageType::ProgramChange,
                  .channel = 0,
                  .number = 7,
                  .target_kind = solo_rig::MidiTargetKind::Preset,
                  .target_id = "lead"},
                 {.message_type = solo_rig::MidiMessageType::ControlChange,
                  .channel = 0,
                  .number = 11,
                  .target_kind = solo_rig::MidiTargetKind::Parameter,
                  .target_id = "drive-1.drive",
                  .minimum = 0.1F,
                  .maximum = 0.9F},
             })
                 .has_value());

    const auto result = controller.Dispatch({
                                                {.type = solo_rig::MidiMessageType::ProgramChange,
                                                 .channel = 0,
                                                 .number = 7,
                                                 .value = 7},
                                                {.type = solo_rig::MidiMessageType::ControlChange,
                                                 .channel = 0,
                                                 .number = 11,
                                                 .value = 127},
                                            },
                                            solo_rig::KeyGesture{.key = "F2"});

    REQUIRE(result.kind == solo_rig::PresetControlDispatchKind::Queued);
    REQUIRE(result.preset_id == "solo");
    const auto* active = controller.ApplyPendingAtControlPoint();
    REQUIRE(active != nullptr);
    REQUIRE(active->preset_id == "solo");
    REQUIRE(active->preset.nodes.front().parameter_values.at("drive") == 0.9F);
}

TEST_CASE("PresetController rejects ambiguous or invalid control input without replacing its active rig") {
    solo_rig::PresetController controller;
    REQUIRE(!controller.RegisterPreset("clean", MakePreset("Clean", 0.2F)).has_value());
    REQUIRE(!controller.RegisterPreset("lead", MakePreset("Lead", 0.8F)).has_value());
    REQUIRE(!controller.QueuePresetActivation("clean").has_value());
    const auto* original = controller.ApplyPendingAtControlPoint();
    REQUIRE(!controller.ConfigureMidiBindings({
                 {.message_type = solo_rig::MidiMessageType::ProgramChange,
                  .channel = 0,
                  .number = 1,
                  .target_kind = solo_rig::MidiTargetKind::Preset,
                  .target_id = "lead"},
                 {.message_type = solo_rig::MidiMessageType::ProgramChange,
                  .channel = 0,
                  .number = 1,
                  .target_kind = solo_rig::MidiTargetKind::Preset,
                  .target_id = "missing"},
             })
                 .has_value());

    const auto ambiguous = controller.Dispatch({{.type = solo_rig::MidiMessageType::ProgramChange,
                                                 .channel = 0,
                                                 .number = 1,
                                                 .value = 1}});
    REQUIRE(ambiguous.kind == solo_rig::PresetControlDispatchKind::Rejected);
    REQUIRE(ambiguous.error == solo_rig::PresetControlError::AmbiguousPresetSelection);
    REQUIRE(controller.ApplyPendingAtControlPoint() == original);

    REQUIRE(!controller.ConfigureMidiBindings({{.message_type = solo_rig::MidiMessageType::ControlChange,
                                                .channel = 0,
                                                .number = 11,
                                                .target_kind = solo_rig::MidiTargetKind::Parameter,
                                                .target_id = "missing.drive"}})
                 .has_value());
    const auto invalid = controller.Dispatch({{.type = solo_rig::MidiMessageType::ControlChange,
                                               .channel = 0,
                                               .number = 11,
                                               .value = 127}});
    REQUIRE(invalid.kind == solo_rig::PresetControlDispatchKind::Rejected);
    REQUIRE(invalid.error == solo_rig::PresetControlError::UnknownParameterTarget);
    REQUIRE(controller.ApplyPendingAtControlPoint() == original);
    REQUIRE(original->preset.nodes.front().parameter_values.at("drive") == 0.2F);
}

TEST_CASE("Preset preparation from a damaged or unknown-version file reports failure and preserves active state") {
    const auto path = std::filesystem::temp_directory_path() / "SoloRigPresetPreparationTest.srigpreset";
    std::error_code error;
    std::filesystem::remove(path, error);
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "{\"schemaVersion\":99,\"name\":\"Future\",\"nodes\":[],\"connections\":[]}";
    }

    solo_rig::PresetPreparationService preparation;
    solo_rig::PresetController controller;
    REQUIRE(!controller.RegisterPreset("clean", MakePreset("Clean", 0.2F)).has_value());
    REQUIRE(!controller.QueuePresetActivation("clean").has_value());
    const auto* original = controller.ApplyPendingAtControlPoint();
    const auto result = preparation.PrepareFromFile("future", path);
    std::filesystem::remove(path, error);

    REQUIRE(std::holds_alternative<solo_rig::PresetPreparationError>(result));
    REQUIRE(std::get<solo_rig::PresetPreparationError>(result) == solo_rig::PresetPreparationError::InvalidSchema);
    REQUIRE(controller.ApplyPendingAtControlPoint() == original);
}

TEST_CASE("Preset preparation reports missing and damaged files without changing the active rig") {
    const auto path = std::filesystem::temp_directory_path() / "SoloRigMissingPresetPreparationTest.srigpreset";
    std::error_code error;
    std::filesystem::remove(path, error);
    solo_rig::PresetPreparationService preparation;

    const auto missing = preparation.PrepareFromFile("missing", path);
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "{not valid JSON";
    }
    const auto damaged = preparation.PrepareFromFile("damaged", path);
    std::filesystem::remove(path, error);

    REQUIRE(std::holds_alternative<solo_rig::PresetPreparationError>(missing));
    REQUIRE(std::get<solo_rig::PresetPreparationError>(missing) == solo_rig::PresetPreparationError::CannotRead);
    REQUIRE(std::holds_alternative<solo_rig::PresetPreparationError>(damaged));
    REQUIRE(std::get<solo_rig::PresetPreparationError>(damaged) == solo_rig::PresetPreparationError::InvalidJson);
}

TEST_CASE("PresetController reports a missing preset target without queuing a replacement") {
    solo_rig::PresetController controller;
    REQUIRE(!controller.RegisterPreset("clean", MakePreset("Clean", 0.2F)).has_value());
    REQUIRE(!controller.QueuePresetActivation("clean").has_value());
    const auto* original = controller.ApplyPendingAtControlPoint();
    REQUIRE(!controller.RegisterShortcut({.gesture = {.key = "F3"},
                                          .action = solo_rig::ShortcutAction::LoadPreset,
                                          .target_id = "missing"})
                 .has_value());

    const auto result = controller.Dispatch({}, solo_rig::KeyGesture{.key = "F3"});

    REQUIRE(result.kind == solo_rig::PresetControlDispatchKind::Rejected);
    REQUIRE(result.error == solo_rig::PresetControlError::UnknownPreset);
    REQUIRE(controller.ApplyPendingAtControlPoint() == original);
}

TEST_CASE("PresetController rejects invalid MIDI and shortcut registration without replacing valid controls") {
    solo_rig::PresetController controller;
    REQUIRE(!controller.RegisterPreset("clean", MakePreset("Clean", 0.2F)).has_value());
    REQUIRE(!controller.RegisterPreset("lead", MakePreset("Lead", 0.8F)).has_value());
    REQUIRE(!controller.QueuePresetActivation("clean").has_value());
    REQUIRE(controller.ApplyPendingAtControlPoint()->preset_id == "clean");
    REQUIRE(!controller.ConfigureMidiBindings({{.message_type = solo_rig::MidiMessageType::ProgramChange,
                                                .channel = 0,
                                                .number = 2,
                                                .target_kind = solo_rig::MidiTargetKind::Preset,
                                                .target_id = "lead"}})
                 .has_value());

    const auto invalid_midi = controller.ConfigureMidiBindings({{.channel = 16, .number = 1, .target_id = "bad"}});
    const auto invalid_shortcut = controller.RegisterShortcut({.gesture = {.key = ""},
                                                               .action = solo_rig::ShortcutAction::LoadPreset,
                                                               .target_id = "lead"});
    const auto result = controller.Dispatch({{.type = solo_rig::MidiMessageType::ProgramChange,
                                              .channel = 0,
                                              .number = 2,
                                              .value = 2}});

    REQUIRE(invalid_midi == solo_rig::PresetControlError::InvalidMidiBinding);
    REQUIRE(invalid_shortcut == solo_rig::PresetControlError::InvalidShortcutBinding);
    REQUIRE(result.kind == solo_rig::PresetControlDispatchKind::Queued);
    REQUIRE(controller.ApplyPendingAtControlPoint()->preset_id == "lead");
}

TEST_CASE("Preset transition policy defines an equal-power 10ms crossfade for later audio-graph integration") {
    const solo_rig::PresetTransitionPolicy transition;
    const auto start = transition.GainsAt(0.0F);
    const auto middle = transition.GainsAt(0.5F);
    const auto end = transition.GainsAt(1.0F);

    REQUIRE(!transition.Validate().has_value());
    REQUIRE(transition.CrossfadeSamples(192000.0) == 1920U);
    REQUIRE(start.outgoing == 1.0F);
    REQUIRE(start.incoming == 0.0F);
    REQUIRE(std::abs(middle.outgoing - 0.7071067F) < 0.0001F);
    REQUIRE(std::abs(middle.incoming - 0.7071067F) < 0.0001F);
    REQUIRE(end.outgoing == 0.0F);
    REQUIRE(end.incoming == 1.0F);
}
