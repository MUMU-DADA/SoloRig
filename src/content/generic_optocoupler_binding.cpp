#include "content/generic_optocoupler_binding.hpp"

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

GenericOptocouplerBindingResult ParseObject(const JsonValue::Object& object) {
    const auto kind = String(object, "kind");
    const auto version = Unsigned(object, "modelVersion");
    const auto dark_resistance = Number(object, "darkResistance");
    const auto lit_resistance = Number(object, "litResistance");
    const auto threshold = Number(object, "emitterThreshold");
    const auto scale = Number(object, "emitterScale");
    const auto exponent = Number(object, "responseExponent");
    const auto attack = Number(object, "attackSeconds");
    const auto release = Number(object, "releaseSeconds");
    const auto maximum_drive = Number(object, "maximumDrive");
    const auto initial = Number(object, "initialIllumination");
    if (!kind.has_value() || *kind != "generic-optocoupler" ||
        !version.has_value() || *version != 1U || !dark_resistance.has_value() ||
        !lit_resistance.has_value() || !threshold.has_value() || !scale.has_value() ||
        !exponent.has_value() || !attack.has_value() || !release.has_value() ||
        !maximum_drive.has_value() || !initial.has_value()) {
        return {.error = GenericOptocouplerBindingError::InvalidModel};
    }
    const GenericOptocouplerModel model{*dark_resistance, *lit_resistance, *threshold,
        *scale, *exponent, *attack, *release, *maximum_drive, *initial};
    if (!ValidateGenericOptocouplerModel(model)) {
        return {.error = GenericOptocouplerBindingError::InvalidModel};
    }
    return {.model = model};
}

} // namespace

GenericOptocouplerBindingResult ParseGenericOptocouplerModel(std::string_view source) {
    const auto parsed = ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        return {.error = GenericOptocouplerBindingError::InvalidJson};
    }
    return ParseObject(*parsed.value->AsObject());
}

GenericOptocouplerBindingResult BindGenericOptocouplerModel(
    const JsonValue::Object& object,
    std::string_view model_id) {
    if (model_id != "generic-optocoupler-v1") {
        return {.error = GenericOptocouplerBindingError::UnauthorizedModel};
    }
    return ParseObject(object);
}

} // namespace solo_rig
