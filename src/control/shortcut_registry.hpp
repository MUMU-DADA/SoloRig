#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace solo_rig {

enum class ShortcutAction {
    LoadPreset,
    SavePreset,
    ToggleTuner,
    BypassSelectedModule,
};

struct KeyGesture {
    std::string key;
    bool control{};
    bool shift{};
    bool alt{};

    [[nodiscard]] bool operator==(const KeyGesture&) const noexcept = default;
};

struct ShortcutBinding {
    KeyGesture gesture;
    ShortcutAction action{ShortcutAction::LoadPreset};
    std::string target_id;
};

enum class ShortcutRegistrationError {
    InvalidGesture,
    DuplicateGesture,
};

class ShortcutRegistry {
public:
    [[nodiscard]] std::optional<ShortcutRegistrationError> Register(ShortcutBinding binding);
    [[nodiscard]] std::optional<ShortcutBinding> Resolve(const KeyGesture& gesture) const;
    void Clear();

private:
    std::vector<ShortcutBinding> bindings_;
};

} // namespace solo_rig
