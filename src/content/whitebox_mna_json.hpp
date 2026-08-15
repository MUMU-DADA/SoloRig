#pragma once

#include "content/circuit_v2.hpp"
#include "content/model_database.hpp"
#include "dsp/mna_solver.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace solo_rig {

enum class WhiteboxMnaJsonError {
    None,
    InvalidSampleRate,
    ParseFailure,
    MnaCompileFailure,
    PrepareFailure,
};

struct WhiteboxMnaJsonPrepareResult final {
    std::optional<MnaPreparedCircuit> circuit;
    std::vector<CircuitLoadError> load_errors;
    std::optional<MnaCompileError> compile_error;
    WhiteboxMnaJsonError error{WhiteboxMnaJsonError::None};
};

// Non-real-time boundary: parses a caller-provided circuit v2 document,
// delegates the immutable netlist to the existing MNA compiler, and prepares
// the resulting circuit at the requested sample rate. No components, models,
// nodes, controls or runtime state are synthesized here.
[[nodiscard]] WhiteboxMnaJsonPrepareResult PrepareWhiteboxMnaV2Json(
    std::string_view source,
    double sample_rate_hz,
    const ModelDatabase* model_database = nullptr);

} // namespace solo_rig
