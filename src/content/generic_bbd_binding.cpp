#include "content/generic_bbd_binding.hpp"

#include <cmath>

namespace solo_rig {

namespace {

const JsonValue* Find(const JsonValue::Object& object, std::string_view key) {
    const auto iterator = object.find(key);
    return iterator == object.end() ? nullptr : &iterator->second;
}

std::optional<double> Number(const JsonValue::Object& object, std::string_view key) {
    const auto* value = Find(object, key);
    if (value == nullptr || value->AsNumber() == nullptr || !std::isfinite(*value->AsNumber())) {
        return std::nullopt;
    }
    return *value->AsNumber();
}

std::optional<std::uint32_t> Unsigned(const JsonValue::Object& object, std::string_view key) {
    const auto value = Number(object, key);
    if (!value.has_value() || *value < 0.0 || *value > 4294967295.0 ||
        std::floor(*value) != *value) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value);
}

std::optional<std::string> String(const JsonValue::Object& object, std::string_view key) {
    const auto* value = Find(object, key);
    if (value == nullptr || value->AsString() == nullptr || value->AsString()->empty()) {
        return std::nullopt;
    }
    return *value->AsString();
}

GenericBbdBindingResult ParseObject(const JsonValue::Object& object) {
    const auto kind = String(object, "kind");
    const auto version = Unsigned(object, "modelVersion");
    const auto buckets = Unsigned(object, "bucketCount");
    const auto clock = Number(object, "clockFrequencyHz");
    const auto efficiency = Number(object, "transferEfficiency");
    const auto feedback = Number(object, "feedback");
    const auto wet = Number(object, "wet");
    const auto dry = Number(object, "dry");
    const auto maximum = Number(object, "maximumInput");
    if (!kind.has_value() || !version.has_value() || *kind != "generic-bbd" || *version != 1U ||
        !buckets.has_value() || !clock.has_value() || !efficiency.has_value() ||
        !feedback.has_value() || !wet.has_value() || !dry.has_value() || !maximum.has_value()) {
        return {.error = GenericBbdBindingError::InvalidModel};
    }
    const GenericBbdModel model{*buckets, *clock, *efficiency, *feedback, *wet, *dry, *maximum};
    if (!ValidateGenericBbdModel(model)) {
        return {.error = GenericBbdBindingError::InvalidModel};
    }
    return {.model = model};
}

} // namespace

GenericBbdBindingResult ParseGenericBbdModel(std::string_view source) {
    const auto parsed = ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        return {.error = GenericBbdBindingError::InvalidJson};
    }
    return ParseObject(*parsed.value->AsObject());
}

GenericBbdBindingResult BindGenericBbdModel(const JsonValue::Object& object,
                                            std::string_view model_id) {
    if (model_id != "generic-bbd-v1") {
        return {.error = GenericBbdBindingError::UnauthorizedModel};
    }
    return ParseObject(object);
}

} // namespace solo_rig
