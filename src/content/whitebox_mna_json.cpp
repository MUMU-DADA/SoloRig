#include "content/whitebox_mna_json.hpp"

#include <cmath>
#include <utility>

namespace solo_rig {

WhiteboxMnaJsonPrepareResult PrepareWhiteboxMnaV2Json(
    std::string_view source,
    double sample_rate_hz,
    const ModelDatabase* model_database) {
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return {.error = WhiteboxMnaJsonError::InvalidSampleRate};
    }

    auto parsed = ParseCircuitV2Json(source);
    if (!parsed.circuit.has_value()) {
        return {.load_errors = std::move(parsed.errors),
                .error = WhiteboxMnaJsonError::ParseFailure};
    }

    const auto compiled = model_database == nullptr
        ? CompileMnaCircuit(*parsed.circuit)
        : CompileMnaCircuit(*parsed.circuit, *model_database);
    if (!compiled.circuit.has_value()) {
        return {.compile_error = compiled.error,
                .error = WhiteboxMnaJsonError::MnaCompileFailure};
    }

    auto circuit = std::move(*compiled.circuit);
    if (!circuit.Prepare(sample_rate_hz)) {
        return {.compile_error = compiled.error,
                .error = WhiteboxMnaJsonError::PrepareFailure};
    }
    return {.circuit = std::move(circuit), .error = WhiteboxMnaJsonError::None};
}

} // namespace solo_rig
