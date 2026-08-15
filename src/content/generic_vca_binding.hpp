#pragma once

#include "content/json.hpp"
#include "dsp/generic_vca.hpp"

#include <optional>
#include <string_view>

namespace solo_rig {

enum class GenericVcaBindingError {
    None,
    InvalidJson,
    InvalidModel,
    UnauthorizedModel,
};

struct GenericVcaBindingResult {
    std::optional<GenericVcaModel> model;
    GenericVcaBindingError error{GenericVcaBindingError::None};
};

[[nodiscard]] GenericVcaBindingResult ParseGenericVcaModel(std::string_view source);
[[nodiscard]] GenericVcaBindingResult BindGenericVcaModel(const JsonValue::Object& object,
                                                          std::string_view model_id);

} // namespace solo_rig
