#pragma once

#include "content/circuit_v2.hpp"
#include "content/model_database.hpp"

namespace solo_rig {

// Prepare-time only: copy an authorized diode/Zener audit record into the
// immutable circuit POD used by MNA. The audio path never retains the record.
[[nodiscard]] bool BindAuditedDiodeModel(const ModelAuditRecord& record,
                                          CircuitDiodeModel& destination) noexcept;

} // namespace solo_rig
