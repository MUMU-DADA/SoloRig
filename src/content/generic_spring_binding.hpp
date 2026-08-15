#pragma once

#include "content/json.hpp"
#include "dsp/generic_spring.hpp"

#include <optional>
#include <string_view>

namespace solo_rig {

enum class GenericSpringBindingError {
    None,
    InvalidJson,
    InvalidModel,
    UnauthorizedModel,
};

struct GenericSpringBindingResult {
    std::optional<GenericSpringModel> model;
    GenericSpringBindingError error{GenericSpringBindingError::None};
};

[[nodiscard]] GenericSpringBindingResult ParseGenericSpringModel(std::string_view source);
[[nodiscard]] GenericSpringBindingResult BindGenericSpringModel(
    const JsonValue::Object& object,
    std::string_view model_id);

} // namespace solo_rig
