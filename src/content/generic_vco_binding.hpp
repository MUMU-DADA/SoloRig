#pragma once

#include "content/json.hpp"
#include "dsp/generic_vco.hpp"

#include <optional>
#include <string_view>

namespace solo_rig {

enum class GenericVcoBindingError {
    None,
    InvalidJson,
    InvalidModel,
    UnauthorizedModel,
};

struct GenericVcoBindingResult {
    std::optional<GenericVcoModel> model;
    GenericVcoBindingError error{GenericVcoBindingError::None};
};

[[nodiscard]] GenericVcoBindingResult ParseGenericVcoModel(std::string_view source);
[[nodiscard]] GenericVcoBindingResult BindGenericVcoModel(const JsonValue::Object& object,
                                                          std::string_view model_id);

} // namespace solo_rig
