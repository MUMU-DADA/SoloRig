#pragma once

namespace solo_rig::m19a1_validation {

struct ChainEvidence {
    bool compiled{};
    bool prepared{};
    bool finite_output{};
    bool reset_deterministic{};
    bool sample_rate_and_oversampling{};
};

[[nodiscard]] ChainEvidence ValidateTremolo();
[[nodiscard]] ChainEvidence ValidateEnvelopeVcaOpticalLike();
[[nodiscard]] ChainEvidence ValidateEnvelopeDetector();
[[nodiscard]] ChainEvidence ValidateOptocouplerResistance();
[[nodiscard]] ChainEvidence ValidateBbdVcoSpring();

} // namespace solo_rig::m19a1_validation
