#include "content/generic_vco_binding.hpp"

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

std::optional<GenericVcoWaveform> Waveform(std::string_view value) {
    if (value == "sine") return GenericVcoWaveform::Sine;
    if (value == "triangle") return GenericVcoWaveform::Triangle;
    if (value == "saw") return GenericVcoWaveform::Saw;
    if (value == "square") return GenericVcoWaveform::Square;
    return std::nullopt;
}

GenericVcoBindingResult ParseObject(const JsonValue::Object& object) {
    const auto kind = String(object, "kind");
    const auto version = Unsigned(object, "modelVersion");
    const auto waveform_name = String(object, "waveform");
    const auto waveform = waveform_name.has_value() ? Waveform(*waveform_name) : std::nullopt;
    const auto base = Number(object, "baseFrequencyHz");
    const auto minimum = Number(object, "minimumFrequencyHz");
    const auto maximum = Number(object, "maximumFrequencyHz");
    const auto volts_per_octave = Number(object, "voltsPerOctave");
    const auto reference = Number(object, "referenceVoltage");
    const auto amplitude = Number(object, "outputAmplitude");
    const auto offset = Number(object, "outputOffset");
    const auto phase = Number(object, "initialPhaseCycles");
    const auto maximum_control = Number(object, "maximumControlVoltage");
    if (!kind.has_value() || *kind != "generic-vco" || !version.has_value() || *version != 1U ||
        !waveform.has_value() || !base.has_value() || !minimum.has_value() ||
        !maximum.has_value() || !volts_per_octave.has_value() || !reference.has_value() ||
        !amplitude.has_value() || !offset.has_value() || !phase.has_value() ||
        !maximum_control.has_value()) {
        return {.error = GenericVcoBindingError::InvalidModel};
    }
    const GenericVcoModel model{*waveform, *base, *minimum, *maximum, *volts_per_octave,
        *reference, *amplitude, *offset, *phase, *maximum_control};
    if (!ValidateGenericVcoModel(model)) {
        return {.error = GenericVcoBindingError::InvalidModel};
    }
    return {.model = model};
}

} // namespace

GenericVcoBindingResult ParseGenericVcoModel(std::string_view source) {
    const auto parsed = ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        return {.error = GenericVcoBindingError::InvalidJson};
    }
    return ParseObject(*parsed.value->AsObject());
}

GenericVcoBindingResult BindGenericVcoModel(const JsonValue::Object& object,
                                            std::string_view model_id) {
    if (model_id != "generic-vco-v1") {
        return {.error = GenericVcoBindingError::UnauthorizedModel};
    }
    return ParseObject(object);
}

} // namespace solo_rig
