#include "content/json.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>

namespace solo_rig {

const std::string* JsonValue::AsString() const noexcept {
    return std::get_if<std::string>(&value);
}

const double* JsonValue::AsNumber() const noexcept {
    return std::get_if<double>(&value);
}

const bool* JsonValue::AsBoolean() const noexcept {
    return std::get_if<bool>(&value);
}

const JsonValue::Array* JsonValue::AsArray() const noexcept {
    return std::get_if<Array>(&value);
}

const JsonValue::Object* JsonValue::AsObject() const noexcept {
    return std::get_if<Object>(&value);
}

namespace {

class Parser {
public:
    explicit Parser(std::string_view source) : source_(source) {}

    [[nodiscard]] JsonParseResult Parse() {
        SkipWhitespace();
        const auto parsed = ParseValue();
        if (!parsed.has_value()) {
            return {.value = std::nullopt, .error = Error("expected a JSON value")};
        }
        SkipWhitespace();
        if (position_ != source_.size()) {
            return {.value = std::nullopt, .error = Error("unexpected trailing content")};
        }
        return {.value = std::move(parsed), .error = std::nullopt};
    }

private:
    [[nodiscard]] std::optional<JsonValue> ParseValue() {
        SkipWhitespace();
        if (position_ == source_.size()) {
            return std::nullopt;
        }
        switch (source_[position_]) {
        case '{':
            return ParseObject();
        case '[':
            return ParseArray();
        case '"': {
            const auto text = ParseString();
            return text.has_value() ? std::optional<JsonValue>(JsonValue{.value = *text}) : std::nullopt;
        }
        case 't':
            return ConsumeLiteral("true", JsonValue{.value = true});
        case 'f':
            return ConsumeLiteral("false", JsonValue{.value = false});
        case 'n':
            return ConsumeLiteral("null", JsonValue{.value = nullptr});
        default:
            return ParseNumber();
        }
    }

    [[nodiscard]] std::optional<JsonValue> ParseObject() {
        ++position_;
        JsonValue::Object object;
        SkipWhitespace();
        if (Consume('}')) {
            return JsonValue{.value = std::move(object)};
        }
        while (position_ < source_.size()) {
            const auto key = ParseString();
            if (!key.has_value()) {
                return std::nullopt;
            }
            SkipWhitespace();
            if (!Consume(':')) {
                return std::nullopt;
            }
            const auto value = ParseValue();
            if (!value.has_value() || !object.emplace(*key, *value).second) {
                return std::nullopt;
            }
            SkipWhitespace();
            if (Consume('}')) {
                return JsonValue{.value = std::move(object)};
            }
            if (!Consume(',')) {
                return std::nullopt;
            }
            SkipWhitespace();
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<JsonValue> ParseArray() {
        ++position_;
        JsonValue::Array array;
        SkipWhitespace();
        if (Consume(']')) {
            return JsonValue{.value = std::move(array)};
        }
        while (position_ < source_.size()) {
            const auto value = ParseValue();
            if (!value.has_value()) {
                return std::nullopt;
            }
            array.push_back(*value);
            SkipWhitespace();
            if (Consume(']')) {
                return JsonValue{.value = std::move(array)};
            }
            if (!Consume(',')) {
                return std::nullopt;
            }
            SkipWhitespace();
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> ParseString() {
        if (!Consume('"')) {
            return std::nullopt;
        }
        std::string text;
        while (position_ < source_.size()) {
            const char character = source_[position_++];
            if (character == '"') {
                return text;
            }
            if (static_cast<unsigned char>(character) < 0x20U) {
                return std::nullopt;
            }
            if (character != '\\') {
                text.push_back(character);
                continue;
            }
            if (position_ == source_.size()) {
                return std::nullopt;
            }
            const char escaped = source_[position_++];
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                text.push_back(escaped);
                break;
            case 'b':
                text.push_back('\b');
                break;
            case 'f':
                text.push_back('\f');
                break;
            case 'n':
                text.push_back('\n');
                break;
            case 'r':
                text.push_back('\r');
                break;
            case 't':
                text.push_back('\t');
                break;
            default:
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<JsonValue> ParseNumber() {
        const auto begin = position_;
        if (Peek('-')) {
            ++position_;
        }
        if (Peek('0')) {
            ++position_;
        } else {
            ConsumeDigits();
        }
        if (Peek('.')) {
            ++position_;
            const auto fractional_begin = position_;
            ConsumeDigits();
            if (position_ == fractional_begin) {
                return std::nullopt;
            }
        }
        if (Peek('e') || Peek('E')) {
            ++position_;
            if (Peek('+') || Peek('-')) {
                ++position_;
            }
            const auto exponent_begin = position_;
            ConsumeDigits();
            if (position_ == exponent_begin) {
                return std::nullopt;
            }
        }
        if (position_ == begin) {
            return std::nullopt;
        }

        double number = 0.0;
        const auto* number_begin = source_.data() + begin;
        const auto* number_end = source_.data() + position_;
        const auto conversion = std::from_chars(number_begin, number_end, number);
        if (conversion.ec != std::errc{} || conversion.ptr != number_end || !std::isfinite(number)) {
            return std::nullopt;
        }
        return JsonValue{.value = number};
    }

    [[nodiscard]] std::optional<JsonValue> ConsumeLiteral(std::string_view literal, JsonValue value) {
        if (source_.substr(position_, literal.size()) != literal) {
            return std::nullopt;
        }
        position_ += literal.size();
        return value;
    }

    void ConsumeDigits() noexcept {
        while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') {
            ++position_;
        }
    }

    void SkipWhitespace() noexcept {
        while (position_ < source_.size() && (source_[position_] == ' ' || source_[position_] == '\n' ||
                                               source_[position_] == '\r' || source_[position_] == '\t')) {
            ++position_;
        }
    }

    [[nodiscard]] bool Consume(char expected) noexcept {
        if (Peek(expected)) {
            ++position_;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool Peek(char expected) const noexcept {
        return position_ < source_.size() && source_[position_] == expected;
    }

    [[nodiscard]] JsonParseError Error(std::string message) const {
        return {.offset = position_, .message = std::move(message)};
    }

    std::string_view source_;
    std::size_t position_{};
};

} // namespace

JsonParseResult ParseJson(std::string_view source) {
    return Parser(source).Parse();
}

std::string EscapeJsonString(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(character);
            break;
        }
    }
    return escaped;
}

} // namespace solo_rig
