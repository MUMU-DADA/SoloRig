#pragma once

#include "content/circuit_v2.hpp"
#include "content/model_database.hpp"

namespace solo_rig {

[[nodiscard]] bool BindAuditedSpeakerLoadModel(
    const ModelAuditRecord& record,
    CircuitSpeakerLoadModel& destination) noexcept;

} // namespace solo_rig
