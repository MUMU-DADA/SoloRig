#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace solo_rig {

struct PedalPosition {
    double x{};
    double y{};
};

class PedalLayout {
public:
    explicit PedalLayout(double grid_size = 16.0);

    [[nodiscard]] bool Add(std::string module_instance_id, PedalPosition position);
    [[nodiscard]] bool Move(std::string_view module_instance_id, PedalPosition position, bool snap_to_grid);
    [[nodiscard]] std::optional<PedalPosition> Find(std::string_view module_instance_id) const;

private:
    [[nodiscard]] double Snap(double value) const noexcept;

    double grid_size_;
    std::unordered_map<std::string, PedalPosition> positions_;
};

} // namespace solo_rig
