#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace solo_rig::standalone {

// Human-editable Standalone route syntax:
//   0:in=0,1;out=0,1|1:in=2;out=3
// The numeric device indices refer to the discovered ASIO driver list.
struct AsioRouteTextEntry final {
    std::size_t driver_index{};
    std::vector<std::uint32_t> inputs;
    std::vector<std::uint32_t> outputs;
};

[[nodiscard]] std::optional<std::vector<AsioRouteTextEntry>> ParseAsioRouteText(
    std::wstring_view text,
    std::size_t driver_count);

} // namespace solo_rig::standalone
