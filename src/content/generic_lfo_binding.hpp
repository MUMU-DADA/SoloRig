#pragma once

#include "content/json.hpp"
#include "dsp/generic_lfo.hpp"

#include <optional>
#include <string_view>

namespace solo_rig {

enum class GenericLfoBindingError {
    None,
    InvalidJson,
    InvalidModel,
    UnauthorizedModel,
};

struct GenericLfoBindingResult {
    std::optional<GenericLfoModel> model;
    GenericLfoBindingError error{GenericLfoBindingError::None};
};

[[nodiscard]] GenericLfoBindingResult ParseGenericLfoModel(std::string_view source);
[[nodiscard]] GenericLfoBindingResult BindGenericLfoModel(const JsonValue::Object& object,
                                                          std::string_view model_id);

} // namespace solo_rig
