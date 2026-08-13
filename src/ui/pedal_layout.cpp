#include "ui/pedal_layout.hpp"

#include <cmath>

namespace solo_rig {

PedalLayout::PedalLayout(double grid_size) : grid_size_(grid_size > 0.0 ? grid_size : 1.0) {}

bool PedalLayout::Add(std::string module_instance_id, PedalPosition position) {
    if (module_instance_id.empty() || !std::isfinite(position.x) || !std::isfinite(position.y)) {
        return false;
    }
    return positions_.emplace(std::move(module_instance_id), position).second;
}

bool PedalLayout::Remove(std::string_view module_instance_id) {
    return positions_.erase(std::string(module_instance_id)) != 0U;
}

bool PedalLayout::Move(std::string_view module_instance_id, PedalPosition position, bool snap_to_grid) {
    const auto iterator = positions_.find(std::string(module_instance_id));
    if (iterator == positions_.end() || !std::isfinite(position.x) || !std::isfinite(position.y)) {
        return false;
    }
    iterator->second = snap_to_grid ? PedalPosition{.x = Snap(position.x), .y = Snap(position.y)} : position;
    return true;
}

std::optional<PedalPosition> PedalLayout::Find(std::string_view module_instance_id) const {
    const auto iterator = positions_.find(std::string(module_instance_id));
    return iterator == positions_.end() ? std::nullopt : std::optional<PedalPosition>(iterator->second);
}

std::vector<std::pair<std::string, PedalPosition>> PedalLayout::Entries() const {
    std::vector<std::pair<std::string, PedalPosition>> result;
    result.reserve(positions_.size());
    for (const auto& [instance_id, position] : positions_) {
        result.emplace_back(instance_id, position);
    }
    return result;
}

double PedalLayout::Snap(double value) const noexcept {
    return std::round(value / grid_size_) * grid_size_;
}

} // namespace solo_rig
