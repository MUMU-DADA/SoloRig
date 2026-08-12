#include "control/shortcut_registry.hpp"

#include <algorithm>

namespace solo_rig {

std::optional<ShortcutRegistrationError> ShortcutRegistry::Register(ShortcutBinding binding) {
    if (binding.gesture.key.empty()) {
        return ShortcutRegistrationError::InvalidGesture;
    }
    if (std::ranges::any_of(bindings_, [&binding](const auto& existing) { return existing.gesture == binding.gesture; })) {
        return ShortcutRegistrationError::DuplicateGesture;
    }
    bindings_.push_back(std::move(binding));
    return std::nullopt;
}

std::optional<ShortcutBinding> ShortcutRegistry::Resolve(const KeyGesture& gesture) const {
    const auto iterator = std::ranges::find_if(bindings_, [&gesture](const auto& binding) {
        return binding.gesture == gesture;
    });
    return iterator == bindings_.end() ? std::nullopt : std::optional<ShortcutBinding>(*iterator);
}

void ShortcutRegistry::Clear() {
    bindings_.clear();
}

} // namespace solo_rig
