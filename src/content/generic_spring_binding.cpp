#include "content/generic_spring_binding.hpp"

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

GenericSpringBindingResult ParseObject(const JsonValue::Object& object) {
    const auto kind = String(object, "kind");
    const auto version = Unsigned(object, "modelVersion");
    const auto mode_count = Unsigned(object, "modeCount");
    const auto base_frequency = Number(object, "baseFrequencyHz");
    const auto mode_spacing = Number(object, "modeSpacingHz");
    const auto dispersion = Number(object, "dispersion");
    const auto decay = Number(object, "decaySeconds");
    const auto damping = Number(object, "highFrequencyDamping");
    const auto drive_gain = Number(object, "driveGain");
    const auto pickup_gain = Number(object, "pickupGain");
    const auto wet = Number(object, "wet");
    const auto dry = Number(object, "dry");
    const auto maximum_input = Number(object, "maximumInput");
    const auto maximum_output = Number(object, "maximumOutput");
    if (!kind.has_value() || *kind != "generic-spring" ||
        !version.has_value() || *version != 1U || !mode_count.has_value() ||
        !base_frequency.has_value() || !mode_spacing.has_value() ||
        !dispersion.has_value() || !decay.has_value() || !damping.has_value() ||
        !drive_gain.has_value() || !pickup_gain.has_value() || !wet.has_value() ||
        !dry.has_value() || !maximum_input.has_value() || !maximum_output.has_value()) {
        return {.error = GenericSpringBindingError::InvalidModel};
    }
    const GenericSpringModel model{*mode_count, *base_frequency, *mode_spacing,
        *dispersion, *decay, *damping, *drive_gain, *pickup_gain, *wet, *dry,
        *maximum_input, *maximum_output};
    if (!ValidateGenericSpringModel(model)) {
        return {.error = GenericSpringBindingError::InvalidModel};
    }
    return {.model = model};
}

} // namespace

GenericSpringBindingResult ParseGenericSpringModel(std::string_view source) {
    const auto parsed = ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        return {.error = GenericSpringBindingError::InvalidJson};
    }
    return ParseObject(*parsed.value->AsObject());
}

GenericSpringBindingResult BindGenericSpringModel(const JsonValue::Object& object,
                                                  std::string_view model_id) {
    if (model_id != "generic-spring-v1") {
        return {.error = GenericSpringBindingError::UnauthorizedModel};
    }
    return ParseObject(object);
}

} // namespace solo_rig
