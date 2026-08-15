#pragma once

#include "content/circuit_v2.hpp"

#include <cstdint>

namespace solo_rig::m19c_fixtures {

// Caller-owned, self-authored generic netlists. These are representative
// structures only; they are not commercial product or SPICE reference data.
[[nodiscard]] CircuitNetlist PassiveTone(std::uint32_t oversampling = 1U,
                                         double potentiometer_position = 0.5);
[[nodiscard]] CircuitNetlist DiodeOverdrive(std::uint32_t oversampling = 1U);
[[nodiscard]] CircuitNetlist BjtBoostFuzz(std::uint32_t oversampling = 1U);
[[nodiscard]] CircuitNetlist JfetBoostFuzz(std::uint32_t oversampling = 1U);
[[nodiscard]] CircuitNetlist MosfetBoostFuzz(std::uint32_t oversampling = 1U);
[[nodiscard]] CircuitNetlist DynamicOpampDrive(std::uint32_t oversampling = 1U);
[[nodiscard]] CircuitNetlist PhaserTone(std::uint32_t oversampling = 1U);

} // namespace solo_rig::m19c_fixtures
