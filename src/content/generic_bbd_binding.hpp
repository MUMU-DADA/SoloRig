#pragma once

#include "content/json.hpp"
#include "dsp/generic_bbd.hpp"

#include <optional>
#include <string_view>

namespace solo_rig {

enum class GenericBbdBindingError {
    None,
    InvalidJson,
    InvalidModel,
    UnauthorizedModel,
};

struct GenericBbdBindingResult {
    std::optional<GenericBbdModel> model;
    GenericBbdBindingError error{GenericBbdBindingError::None};
};

[[nodiscard]] GenericBbdBindingResult ParseGenericBbdModel(std::string_view source);
[[nodiscard]] GenericBbdBindingResult BindGenericBbdModel(const JsonValue::Object& object,
                                                          std::string_view model_id);

} // namespace solo_rig
