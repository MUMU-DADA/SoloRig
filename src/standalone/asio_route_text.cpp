#include "standalone/asio_route_text.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace solo_rig::standalone {
namespace {

std::wstring_view TrimView(std::wstring_view text) {
    while (!text.empty() && (text.front() == L' ' || text.front() == L'\t')) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == L' ' || text.back() == L'\t')) {
        text.remove_suffix(1U);
    }
    return text;
}

std::optional<std::vector<std::uint32_t>> ParseChannelList(std::wstring_view text) {
    std::vector<std::uint32_t> channels;
    std::uint64_t value = 0U;
    bool have_digit = false;
    bool need_value = true;
    for (const auto character : text) {
        if (character >= L'0' && character <= L'9') {
            have_digit = true;
            need_value = false;
            value = value * 10U + static_cast<std::uint64_t>(character - L'0');
            if (value > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            continue;
        }
        if (character == L' ' || character == L'\t') {
            continue;
        }
        if (character != L',' || !have_digit || need_value) {
            return std::nullopt;
        }
        channels.push_back(static_cast<std::uint32_t>(value));
        value = 0U;
        have_digit = false;
        need_value = true;
    }
    if (!have_digit || need_value) {
        return std::nullopt;
    }
    channels.push_back(static_cast<std::uint32_t>(value));
    return channels.empty() ? std::nullopt : std::optional<std::vector<std::uint32_t>>(std::move(channels));
}

} // namespace

std::optional<std::vector<AsioRouteTextEntry>> ParseAsioRouteText(std::wstring_view text,
                                                                  std::size_t driver_count) {
    std::vector<AsioRouteTextEntry> entries;
    std::size_t segment_start = 0U;
    while (segment_start < text.size()) {
        const auto separator = text.find(L'|', segment_start);
        const auto segment_end = separator == std::wstring_view::npos ? text.size() : separator;
        const auto segment = TrimView(text.substr(segment_start, segment_end - segment_start));
        const auto colon = segment.find(L':');
        if (colon == std::wstring_view::npos || colon == 0U) {
            return std::nullopt;
        }
        std::size_t driver_index = 0U;
        bool have_index_digit = false;
        for (const auto character : segment.substr(0U, colon)) {
            if (character == L' ' || character == L'\t') {
                continue;
            }
            if (character < L'0' || character > L'9') {
                return std::nullopt;
            }
            have_index_digit = true;
            const auto digit = static_cast<std::size_t>(character - L'0');
            if (driver_index > (std::numeric_limits<std::size_t>::max() - digit) / 10U) {
                return std::nullopt;
            }
            driver_index = driver_index * 10U + digit;
            if (driver_index >= driver_count) {
                return std::nullopt;
            }
        }
        if (!have_index_digit || std::any_of(entries.begin(), entries.end(), [driver_index](const auto& entry) {
                return entry.driver_index == driver_index;
            })) {
            return std::nullopt;
        }
        AsioRouteTextEntry entry{.driver_index = driver_index};
        const auto body = segment.substr(colon + 1U);
        std::size_t field_start = 0U;
        while (field_start < body.size()) {
            const auto field_separator = body.find(L';', field_start);
            const auto field_end = field_separator == std::wstring_view::npos ? body.size() : field_separator;
            const auto field = TrimView(body.substr(field_start, field_end - field_start));
            const auto is_input = field.starts_with(L"in=");
            const auto is_output = field.starts_with(L"out=");
            if (!is_input && !is_output) {
                return std::nullopt;
            }
            const auto channels = ParseChannelList(field.substr(is_input ? 3U : 4U));
            if (!channels.has_value() || channels->empty()) {
                return std::nullopt;
            }
            if (is_input) {
                if (!entry.inputs.empty()) {
                    return std::nullopt;
                }
                entry.inputs = *channels;
            } else {
                if (!entry.outputs.empty()) {
                    return std::nullopt;
                }
                entry.outputs = *channels;
            }
            if (field_separator == std::wstring_view::npos) {
                break;
            }
            field_start = field_separator + 1U;
        }
        if (entry.inputs.empty() || entry.outputs.empty()) {
            return std::nullopt;
        }
        entries.push_back(std::move(entry));
        if (separator == std::wstring_view::npos) {
            break;
        }
        segment_start = separator + 1U;
    }
    return entries.empty() ? std::nullopt : std::optional<std::vector<AsioRouteTextEntry>>(std::move(entries));
}

} // namespace solo_rig::standalone
