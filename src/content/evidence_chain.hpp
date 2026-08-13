#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace solo_rig {

enum class EvidencePackageStatus { SchemaOnly, Blocked, Passed };
enum class EvidenceSectionStatus { SchemaOnly, Blocked, Passed };
enum class EvidenceEvaluation { SchemaOnly, Blocked, Passed };

enum class EvidenceChainError {
    InvalidJson,
    UnsupportedVersion,
    InvalidSchema,
    MissingField,
    InvalidField,
    NonFiniteValue,
    InvalidStatus,
    ThresholdExceeded,
    MissingBlockedReference,
};

struct EvidenceSection {
    EvidenceSectionStatus status{EvidenceSectionStatus::SchemaOnly};
    std::uint32_t sample_count{};
    std::optional<double> max_abs_error;
    std::optional<double> threshold;
    std::string reference_ref;
};

struct EvidenceConvergenceCase {
    std::string id;
    EvidenceSectionStatus status{EvidenceSectionStatus::SchemaOnly};
    std::uint32_t iterations{};
    std::optional<std::string> expected_error;
};

struct EvidenceConvergence {
    EvidenceSectionStatus status{EvidenceSectionStatus::SchemaOnly};
    std::vector<EvidenceConvergenceCase> cases;
};

struct EvidenceRunMetadata {
    std::string tool_version;
    double sample_rate_hz{};
    std::uint32_t oversampling{};
    std::uint64_t random_seed{};
};

struct EvidencePackage {
    std::string module_id;
    std::string module_version;
    EvidencePackageStatus status{EvidencePackageStatus::SchemaOnly};
    std::uint32_t netlist_schema_version{};
    std::string netlist_source_ref;
    std::string input_hash;
    std::vector<std::string> model_audit_refs;
    EvidenceSection dc_operating_point;
    EvidenceSection frequency_response;
    EvidenceSection static_curve;
    EvidenceSection dynamic_response;
    EvidenceSection spice;
    EvidenceSection golden;
    EvidenceConvergence convergence;
    double dc_threshold{};
    double frequency_threshold{};
    double dynamic_threshold{};
    EvidenceRunMetadata run;
};

struct EvidenceParseResult {
    std::optional<EvidencePackage> package;
    std::optional<EvidenceChainError> error;
};

struct EvidenceComparisonResult {
    double max_abs_error{};
    bool passed{false};
};

[[nodiscard]] EvidenceParseResult ParseEvidencePackageJson(std::string_view source);
[[nodiscard]] EvidenceEvaluation EvaluateEvidencePackage(const EvidencePackage& package) noexcept;
// Non-real-time validation helper. It borrows caller-owned samples and does
// not allocate, access files, or enter the audio Process path.
[[nodiscard]] EvidenceComparisonResult CompareEvidenceSamples(std::span<const double> measured,
                                                               std::span<const double> reference,
                                                               double threshold) noexcept;

} // namespace solo_rig
