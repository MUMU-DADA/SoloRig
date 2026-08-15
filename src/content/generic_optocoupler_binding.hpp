#pragma once

#include "content/json.hpp"
#include "dsp/generic_optocoupler.hpp"

#include <optional>
#include <string_view>

namespace solo_rig {

enum class GenericOptocouplerBindingError {
    None,
    InvalidJson,
    InvalidModel,
    UnauthorizedModel,
};

struct GenericOptocouplerBindingResult {
    std::optional<GenericOptocouplerModel> model;
    GenericOptocouplerBindingError error{GenericOptocouplerBindingError::None};
};

[[nodiscard]] GenericOptocouplerBindingResult ParseGenericOptocouplerModel(
    std::string_view source);
[[nodiscard]] GenericOptocouplerBindingResult BindGenericOptocouplerModel(
    const JsonValue::Object& object,
    std::string_view model_id);

} // namespace solo_rig
