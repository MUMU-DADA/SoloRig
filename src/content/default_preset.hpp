#pragma once

#include "content/preset.hpp"

namespace solo_rig {

// Shared product baseline used by the editor and hosts. Host-specific state
// may override parameters, but the module graph itself must stay identical.
[[nodiscard]] RigPreset DefaultRigPreset();

} // namespace solo_rig
