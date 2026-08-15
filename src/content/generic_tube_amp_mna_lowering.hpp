#pragma once

#include "content/circuit_v2.hpp"
#include "content/generic_tube_amp_compile.hpp"
#include "content/model_database.hpp"
#include "dsp/mna_solver.hpp"

#include <optional>

namespace solo_rig {

enum class GenericTubeAmpMnaLoweringError : std::uint8_t {
    None,
    PlanNotReady,
    NetlistMismatch,
    InvalidModel,
    MissingComponent,
    ExtraComponent,
    InvalidPinOrder,
    InvalidSupply,
    InvalidPort,
    MnaCompileFailure,
};

struct GenericTubeAmpMnaLoweringResult {
    std::optional<MnaPreparedCircuit> circuit;
    GenericTubeAmpMnaLoweringError error{GenericTubeAmpMnaLoweringError::None};
};

// Validates a Ready typed plan against caller-owned CircuitNetlist data, then
// delegates compilation to the existing MNA compiler. No components, nodes,
// models, values, or runtime state are synthesized here.
[[nodiscard]] GenericTubeAmpMnaLoweringResult LowerGenericTubeAmpToMna(
    const GenericTubeAmpCompilePlan& plan,
    const CircuitNetlist& netlist,
    const ModelDatabase& model_database);

} // namespace solo_rig
