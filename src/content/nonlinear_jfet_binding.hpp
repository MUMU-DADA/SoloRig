#pragma once

#include "content/circuit_v2.hpp"
#include "content/model_database.hpp"

namespace solo_rig {
[[nodiscard]] bool BindAuditedJfetModel(const ModelAuditRecord& record,
                                        CircuitJfetModel& destination) noexcept;
} // namespace solo_rig
