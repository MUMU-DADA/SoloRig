#include "content/generic_vca_binding.hpp"

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

GenericVcaBindingResult ParseObject(const JsonValue::Object& object) {
    const auto kind = String(object, "kind");
    const auto version = Unsigned(object, "modelVersion");
    const auto reference_control = Number(object, "referenceControlVoltage");
    const auto reference_gain = Number(object, "referenceGain");
    const auto gain_per_volt = Number(object, "gainDecibelsPerVolt");
    const auto minimum_gain = Number(object, "minimumGain");
    const auto maximum_gain = Number(object, "maximumGain");
    const auto smoothing = Number(object, "controlSmoothingSeconds");
    const auto initial_control = Number(object, "initialControlVoltage");
    const auto maximum_control = Number(object, "maximumControlVoltage");
    const auto maximum_input = Number(object, "maximumInput");
    const auto maximum_output = Number(object, "maximumOutput");
    if (!kind.has_value() || *kind != "generic-vca" || !version.has_value() || *version != 1U ||
        !reference_control.has_value() || !reference_gain.has_value() ||
        !gain_per_volt.has_value() || !minimum_gain.has_value() || !maximum_gain.has_value() ||
        !smoothing.has_value() || !initial_control.has_value() || !maximum_control.has_value() ||
        !maximum_input.has_value() || !maximum_output.has_value()) {
        return {.error = GenericVcaBindingError::InvalidModel};
    }
    const GenericVcaModel model{*reference_control, *reference_gain, *gain_per_volt,
        *minimum_gain, *maximum_gain, *smoothing, *initial_control, *maximum_control,
        *maximum_input, *maximum_output};
    if (!ValidateGenericVcaModel(model)) {
        return {.error = GenericVcaBindingError::InvalidModel};
    }
    return {.model = model};
}

} // namespace

GenericVcaBindingResult ParseGenericVcaModel(std::string_view source) {
    const auto parsed = ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        return {.error = GenericVcaBindingError::InvalidJson};
    }
    return ParseObject(*parsed.value->AsObject());
}

GenericVcaBindingResult BindGenericVcaModel(const JsonValue::Object& object,
                                            std::string_view model_id) {
    if (model_id != "generic-vca-v1") {
        return {.error = GenericVcaBindingError::UnauthorizedModel};
    }
    return ParseObject(object);
}

} // namespace solo_rig
