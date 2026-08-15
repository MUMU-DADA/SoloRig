#pragma once

#include "content/generic_tube_amp_compile.hpp"
#include "content/circuit_v2.hpp"

namespace solo_rig::fixtures {

[[nodiscard]] GenericTubeAmpCompileRequest MakeMinimalGenericTubeAmpRequest() noexcept;
[[nodiscard]] CircuitNetlist MakeMinimalGenericTubeAmpCircuit();

} // namespace solo_rig::fixtures
