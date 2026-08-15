#include "content/generic_lfo_binding.hpp"

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

std::optional<GenericLfoWaveform> Waveform(std::string_view value) {
    if (value == "sine") return GenericLfoWaveform::Sine;
    if (value == "triangle") return GenericLfoWaveform::Triangle;
    if (value == "saw") return GenericLfoWaveform::Saw;
    if (value == "square") return GenericLfoWaveform::Square;
    return std::nullopt;
}

GenericLfoBindingResult ParseObject(const JsonValue::Object& object) {
    const auto kind = String(object, "kind");
    const auto version = Unsigned(object, "modelVersion");
    const auto waveform_name = String(object, "waveform");
    const auto waveform = waveform_name.has_value() ? Waveform(*waveform_name) : std::nullopt;
    const auto frequency = Number(object, "frequencyHz");
    const auto amplitude = Number(object, "outputAmplitude");
    const auto offset = Number(object, "outputOffset");
    const auto maximum = Number(object, "maximumOutput");
    const auto phase = Number(object, "initialPhaseCycles");
    const auto duty = Number(object, "dutyCycle");
    if (!kind.has_value() || *kind != "generic-lfo" || !version.has_value() || *version != 1U ||
        !waveform.has_value() || !frequency.has_value() || !amplitude.has_value() ||
        !offset.has_value() || !maximum.has_value() || !phase.has_value() || !duty.has_value()) {
        return {.error = GenericLfoBindingError::InvalidModel};
    }
    const GenericLfoModel model{*waveform, *frequency, *amplitude, *offset,
        *maximum, *phase, *duty};
    if (!ValidateGenericLfoModel(model)) {
        return {.error = GenericLfoBindingError::InvalidModel};
    }
    return {.model = model};
}

} // namespace

GenericLfoBindingResult ParseGenericLfoModel(std::string_view source) {
    const auto parsed = ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        return {.error = GenericLfoBindingError::InvalidJson};
    }
    return ParseObject(*parsed.value->AsObject());
}

GenericLfoBindingResult BindGenericLfoModel(const JsonValue::Object& object,
                                            std::string_view model_id) {
    if (model_id != "generic-lfo-v1") {
        return {.error = GenericLfoBindingError::UnauthorizedModel};
    }
    return ParseObject(object);
}

} // namespace solo_rig
