#include "control/shortcut_registry.hpp"
#include "test_framework.hpp"

TEST_CASE("ShortcutRegistry resolves a registered preset action") {
    solo_rig::ShortcutRegistry shortcuts;
    const solo_rig::ShortcutBinding binding{
        .gesture = {.key = "F1", .control = true},
        .action = solo_rig::ShortcutAction::LoadPreset,
        .target_id = "preset-1",
    };

    REQUIRE(!shortcuts.Register(binding).has_value());
    const auto resolved = shortcuts.Resolve(binding.gesture);
    REQUIRE(resolved.has_value());
    REQUIRE(resolved->target_id == "preset-1");
}

TEST_CASE("ShortcutRegistry rejects duplicate or blank keyboard gestures") {
    solo_rig::ShortcutRegistry shortcuts;
    const solo_rig::ShortcutBinding valid{.gesture = {.key = "T"}, .action = solo_rig::ShortcutAction::ToggleTuner};

    REQUIRE(!shortcuts.Register(valid).has_value());
    REQUIRE(shortcuts.Register(valid).value() == solo_rig::ShortcutRegistrationError::DuplicateGesture);
    REQUIRE(shortcuts.Register({.gesture = {.key = ""}}).value() == solo_rig::ShortcutRegistrationError::InvalidGesture);
}
