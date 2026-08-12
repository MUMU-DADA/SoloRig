#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace solo_rig {

struct JsonValue {
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue, std::less<>>;
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    Storage value{nullptr};

    [[nodiscard]] const std::string* AsString() const noexcept;
    [[nodiscard]] const double* AsNumber() const noexcept;
    [[nodiscard]] const bool* AsBoolean() const noexcept;
    [[nodiscard]] const Array* AsArray() const noexcept;
    [[nodiscard]] const Object* AsObject() const noexcept;
};

struct JsonParseError {
    std::size_t offset{};
    std::string message;
};

struct JsonParseResult {
    std::optional<JsonValue> value;
    std::optional<JsonParseError> error;
};

[[nodiscard]] JsonParseResult ParseJson(std::string_view source);
[[nodiscard]] std::string EscapeJsonString(std::string_view value);

} // namespace solo_rig
