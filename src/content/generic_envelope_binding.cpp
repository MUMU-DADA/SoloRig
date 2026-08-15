#include "content/generic_envelope_binding.hpp"

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

GenericEnvelopeBindingResult ParseObject(const JsonValue::Object& object) {
    const auto kind = String(object, "kind");
    const auto version = Unsigned(object, "modelVersion");
    const auto input_gain = Number(object, "inputGain");
    const auto attack = Number(object, "attackSeconds");
    const auto release = Number(object, "releaseSeconds");
    const auto ceiling = Number(object, "detectorCeiling");
    const auto scale = Number(object, "outputScale");
    const auto maximum_input = Number(object, "maximumInput");
    const auto maximum_output = Number(object, "maximumOutput");
    const auto initial = Number(object, "initialEnvelope");
    if (!kind.has_value() || *kind != "generic-envelope" || !version.has_value() ||
        *version != 1U || !input_gain.has_value() || !attack.has_value() ||
        !release.has_value() || !ceiling.has_value() || !scale.has_value() ||
        !maximum_input.has_value() || !maximum_output.has_value() || !initial.has_value()) {
        return {.error = GenericEnvelopeBindingError::InvalidModel};
    }
    const GenericEnvelopeModel model{*input_gain, *attack, *release, *ceiling,
        *scale, *maximum_input, *maximum_output, *initial};
    if (!ValidateGenericEnvelopeModel(model)) {
        return {.error = GenericEnvelopeBindingError::InvalidModel};
    }
    return {.model = model};
}

} // namespace

GenericEnvelopeBindingResult ParseGenericEnvelopeModel(std::string_view source) {
    const auto parsed = ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        return {.error = GenericEnvelopeBindingError::InvalidJson};
    }
    return ParseObject(*parsed.value->AsObject());
}

GenericEnvelopeBindingResult BindGenericEnvelopeModel(const JsonValue::Object& object,
                                                       std::string_view model_id) {
    if (model_id != "generic-envelope-v1") {
        return {.error = GenericEnvelopeBindingError::UnauthorizedModel};
    }
    return ParseObject(object);
}

} // namespace solo_rig
