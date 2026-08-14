#pragma once

#include "content/model_database.hpp"
#include "content/circuit_v2.hpp"

namespace solo_rig {

[[nodiscard]] bool BindAuditedDynamicOpAmpModel(const ModelAuditRecord& record,
                                                CircuitAmplifierModel& destination) noexcept;

} // namespace solo_rig
