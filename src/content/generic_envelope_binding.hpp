#pragma once

#include "content/json.hpp"
#include "dsp/generic_envelope.hpp"

#include <optional>
#include <string_view>

namespace solo_rig {

enum class GenericEnvelopeBindingError {
    None,
    InvalidJson,
    InvalidModel,
    UnauthorizedModel,
};

struct GenericEnvelopeBindingResult {
    std::optional<GenericEnvelopeModel> model;
    GenericEnvelopeBindingError error{GenericEnvelopeBindingError::None};
};

[[nodiscard]] GenericEnvelopeBindingResult ParseGenericEnvelopeModel(std::string_view source);
[[nodiscard]] GenericEnvelopeBindingResult BindGenericEnvelopeModel(
    const JsonValue::Object& object, std::string_view model_id);

} // namespace solo_rig
