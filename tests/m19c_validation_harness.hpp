#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace solo_rig::m19c_validation {

enum class RepresentativeStatus : std::uint8_t {
    RunnableGeneric,
    StableRefusal,
};

enum class RepresentativeCategory : std::uint8_t {
    PassiveTone,
    DiodeOverdrive,
    BjtBoostFuzz,
    JfetBoostFuzz,
    MosfetBoostFuzz,
    DynamicOpampDrive,
    Tremolo,
    OpticalLikeCompression,
    EnvelopeDetector,
    OptocouplerResistance,
    BbdVcoFeedback,
    TubeAmp,
    Wah,
    Phaser,
    ChorusFlanger,
    OpticalHardware,
};

inline constexpr std::size_t kRepresentativeCategoryCount = 16U;

struct RepresentativeEvidence final {
    RepresentativeCategory category{};
    RepresentativeStatus status{RepresentativeStatus::StableRefusal};
    bool compiled{};
    bool prepared{};
    bool finite_nontrivial{};
    bool reset_deterministic{};
    bool sample_rate_and_oversampling{};
    bool rejection_boundary{};
    std::string_view refusal_reason{};
};

[[nodiscard]] std::array<RepresentativeEvidence, kRepresentativeCategoryCount>
ValidateRepresentativeCoverage();

} // namespace solo_rig::m19c_validation
