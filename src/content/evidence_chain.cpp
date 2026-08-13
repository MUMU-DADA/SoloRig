#include "content/evidence_chain.hpp"

#include "content/json.hpp"

#include <cmath>
#include <algorithm>
#include <limits>
#include <string_view>
#include <unordered_set>

namespace solo_rig {

namespace {

const JsonValue* Find(const JsonValue::Object& object, std::string_view key) {
    const auto iterator = object.find(key);
    return iterator == object.end() ? nullptr : &iterator->second;
}

std::optional<std::string> StringValue(const JsonValue* value) {
    if (value == nullptr || value->AsString() == nullptr || value->AsString()->empty()) return std::nullopt;
    return *value->AsString();
}

std::optional<double> NumberValue(const JsonValue* value) {
    if (value == nullptr || value->AsNumber() == nullptr || !std::isfinite(*value->AsNumber())) return std::nullopt;
    return *value->AsNumber();
}

std::optional<std::uint32_t> UnsignedValue(const JsonValue* value) {
    const auto number = NumberValue(value);
    if (!number.has_value() || *number < 0.0 || *number > static_cast<double>(std::numeric_limits<std::uint32_t>::max()) ||
        std::floor(*number) != *number) return std::nullopt;
    return static_cast<std::uint32_t>(*number);
}

std::optional<EvidencePackageStatus> PackageStatus(std::string_view value) {
    if (value == "schema-only") return EvidencePackageStatus::SchemaOnly;
    if (value == "blocked") return EvidencePackageStatus::Blocked;
    if (value == "passed") return EvidencePackageStatus::Passed;
    return std::nullopt;
}

std::optional<EvidenceSectionStatus> SectionStatus(std::string_view value) {
    if (value == "schema-only") return EvidenceSectionStatus::SchemaOnly;
    if (value == "blocked") return EvidenceSectionStatus::Blocked;
    if (value == "passed") return EvidenceSectionStatus::Passed;
    return std::nullopt;
}

struct SectionResult {
    std::optional<EvidenceSection> section;
    std::optional<EvidenceChainError> error;
};

SectionResult ParseSection(const JsonValue* value, bool comparison) {
    const auto* object = value == nullptr ? nullptr : value->AsObject();
    const auto status_string = object == nullptr ? std::nullopt : StringValue(Find(*object, "status"));
    if (!status_string.has_value()) return {.section = std::nullopt, .error = EvidenceChainError::MissingField};
    const auto status = SectionStatus(*status_string);
    if (!status.has_value()) return {.section = std::nullopt, .error = EvidenceChainError::InvalidStatus};
    EvidenceSection section{.status = *status};
    if (*status == EvidenceSectionStatus::Blocked) {
        if (comparison) {
            section.reference_ref = StringValue(Find(*object, "referenceRef")).value_or("");
        }
        return {.section = std::move(section), .error = std::nullopt};
    }
    const auto sample_count = UnsignedValue(Find(*object, "sampleCount"));
    if (!sample_count.has_value()) return {.section = std::nullopt, .error = EvidenceChainError::MissingField};
    section.sample_count = *sample_count;
    if (*status == EvidenceSectionStatus::Passed || comparison) {
        const auto max_error = NumberValue(Find(*object, "maxAbsError"));
        const auto threshold = NumberValue(Find(*object, "threshold"));
        if (!max_error.has_value() || !threshold.has_value()) {
            if (Find(*object, "maxAbsError") != nullptr || Find(*object, "threshold") != nullptr) {
                return {.section = std::nullopt, .error = EvidenceChainError::NonFiniteValue};
            }
            return {.section = std::nullopt, .error = EvidenceChainError::MissingField};
        }
        if (*max_error < 0.0 || *threshold <= 0.0) {
            return {.section = std::nullopt, .error = EvidenceChainError::InvalidField};
        }
        if (*max_error > *threshold) {
            return {.section = std::nullopt, .error = EvidenceChainError::ThresholdExceeded};
        }
        section.max_abs_error = *max_error;
        section.threshold = *threshold;
    }
    if (comparison) {
        section.reference_ref = StringValue(Find(*object, "referenceRef")).value_or("");
        if (*status == EvidenceSectionStatus::Passed && section.reference_ref.empty()) {
            return {.section = std::nullopt, .error = EvidenceChainError::MissingField};
        }
    }
    return {.section = std::move(section), .error = std::nullopt};
}

EvidenceParseResult Failure(EvidenceChainError error) {
    return {.package = std::nullopt, .error = error};
}

} // namespace

EvidenceParseResult ParseEvidencePackageJson(std::string_view source) {
    const auto parsed = ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) return Failure(EvidenceChainError::InvalidJson);
    const auto& object = *parsed.value->AsObject();
    const auto version = UnsignedValue(Find(object, "evidenceSchemaVersion"));
    if (!version.has_value() || *version != 1U) return Failure(EvidenceChainError::UnsupportedVersion);
    const auto module_id = StringValue(Find(object, "moduleId"));
    const auto module_version = StringValue(Find(object, "moduleVersion"));
    const auto status_string = StringValue(Find(object, "status"));
    if (!module_id.has_value() || !module_version.has_value() || !status_string.has_value()) {
        return Failure(EvidenceChainError::MissingField);
    }
    const auto status = PackageStatus(*status_string);
    if (!status.has_value()) return Failure(EvidenceChainError::InvalidStatus);
    const auto* netlist = Find(object, "netlist");
    const auto* netlist_object = netlist == nullptr ? nullptr : netlist->AsObject();
    const auto netlist_version = netlist_object == nullptr ? std::nullopt : UnsignedValue(Find(*netlist_object, "schemaVersion"));
    const auto netlist_source = netlist_object == nullptr ? std::nullopt : StringValue(Find(*netlist_object, "sourceRef"));
    const auto input_hash = netlist_object == nullptr ? std::nullopt : StringValue(Find(*netlist_object, "inputHash"));
    if (!netlist_version.has_value() || *netlist_version != 2U || !netlist_source.has_value() || !input_hash.has_value() ||
        input_hash->empty()) return Failure(EvidenceChainError::MissingField);
    const auto* model_refs = Find(object, "modelAuditRefs");
    const auto* model_array = model_refs == nullptr ? nullptr : model_refs->AsArray();
    if (model_array == nullptr || model_array->empty()) return Failure(EvidenceChainError::MissingField);
    std::vector<std::string> references;
    std::unordered_set<std::string> unique_refs;
    for (const auto& reference : *model_array) {
        const auto value = StringValue(&reference);
        if (!value.has_value() || !unique_refs.insert(*value).second) return Failure(EvidenceChainError::InvalidField);
        references.push_back(*value);
    }
    auto parse_section = [&](std::string_view key, bool comparison) -> SectionResult {
        return ParseSection(Find(object, key), comparison);
    };
    const auto dc = parse_section("dcOperatingPoint", false);
    const auto frequency = parse_section("frequencyResponse", false);
    const auto statics = parse_section("staticCurve", false);
    const auto dynamic = parse_section("dynamicResponse", false);
    const auto spice = parse_section("spiceComparison", true);
    const auto golden = parse_section("goldenComparison", true);
    for (const auto& result : {dc, frequency, statics, dynamic, spice, golden}) {
        if (result.error.has_value()) return Failure(*result.error);
    }
    const auto* convergence_object = Find(object, "convergence");
    const auto* convergence = convergence_object == nullptr ? nullptr : convergence_object->AsObject();
    const auto convergence_status = convergence == nullptr ? std::nullopt : StringValue(Find(*convergence, "status"));
    const auto* cases = convergence == nullptr ? nullptr : Find(*convergence, "cases");
    const auto* case_array = cases == nullptr ? nullptr : cases->AsArray();
    if (!convergence_status.has_value() || case_array == nullptr) return Failure(EvidenceChainError::MissingField);
    const auto parsed_convergence_status = SectionStatus(*convergence_status);
    if (!parsed_convergence_status.has_value()) return Failure(EvidenceChainError::InvalidStatus);
    EvidenceConvergence convergence_value{.status = *parsed_convergence_status};
    for (const auto& case_value : *case_array) {
        const auto* case_object = case_value.AsObject();
        const auto id = case_object == nullptr ? std::nullopt : StringValue(Find(*case_object, "id"));
        const auto case_status_string = case_object == nullptr ? std::nullopt : StringValue(Find(*case_object, "status"));
        const auto iterations = case_object == nullptr ? std::nullopt : UnsignedValue(Find(*case_object, "iterations"));
        if (!id.has_value() || !case_status_string.has_value()) return Failure(EvidenceChainError::MissingField);
        const auto case_status = SectionStatus(*case_status_string);
        if (!case_status.has_value()) return Failure(EvidenceChainError::InvalidStatus);
        if (*case_status != EvidenceSectionStatus::Blocked && !iterations.has_value()) {
            return Failure(EvidenceChainError::MissingField);
        }
        EvidenceConvergenceCase parsed_case{.id = *id, .status = *case_status,
                                            .iterations = iterations.value_or(0U)};
        if (const auto expected = StringValue(Find(*case_object, "expectedError")); expected.has_value()) {
            parsed_case.expected_error = *expected;
        }
        convergence_value.cases.push_back(std::move(parsed_case));
    }
    const auto* threshold_object = Find(object, "errorThresholds");
    const auto* thresholds = threshold_object == nullptr ? nullptr : threshold_object->AsObject();
    const auto dc_threshold = thresholds == nullptr ? std::nullopt : NumberValue(Find(*thresholds, "dcAbsolute"));
    const auto frequency_threshold = thresholds == nullptr ? std::nullopt : NumberValue(Find(*thresholds, "frequencyMagnitude"));
    const auto dynamic_threshold = thresholds == nullptr ? std::nullopt : NumberValue(Find(*thresholds, "dynamicAbsolute"));
    if (!dc_threshold.has_value() || !frequency_threshold.has_value() || !dynamic_threshold.has_value() ||
        *dc_threshold <= 0.0 || *frequency_threshold <= 0.0 || *dynamic_threshold <= 0.0) {
        return Failure(EvidenceChainError::NonFiniteValue);
    }
    const auto* run_object = Find(object, "run");
    const auto* run = run_object == nullptr ? nullptr : run_object->AsObject();
    const auto tool_version = run == nullptr ? std::nullopt : StringValue(Find(*run, "toolVersion"));
    const auto sample_rate = run == nullptr ? std::nullopt : NumberValue(Find(*run, "sampleRateHz"));
    const auto oversampling = run == nullptr ? std::nullopt : UnsignedValue(Find(*run, "oversampling"));
    const auto random_seed_number = run == nullptr ? std::nullopt : NumberValue(Find(*run, "randomSeed"));
    if (!tool_version.has_value() || !sample_rate.has_value() || !oversampling.has_value() || !random_seed_number.has_value() ||
        *sample_rate <= 0.0 || *oversampling == 0U || *oversampling > 16U || *random_seed_number < 0.0 ||
        std::floor(*random_seed_number) != *random_seed_number) {
        return Failure(random_seed_number.has_value() ? EvidenceChainError::InvalidField : EvidenceChainError::NonFiniteValue);
    }
    EvidencePackage package{.module_id = *module_id,
                            .module_version = *module_version,
                            .status = *status,
                            .netlist_schema_version = *netlist_version,
                            .netlist_source_ref = *netlist_source,
                            .input_hash = *input_hash,
                            .model_audit_refs = std::move(references),
                            .dc_operating_point = *dc.section,
                            .frequency_response = *frequency.section,
                            .static_curve = *statics.section,
                            .dynamic_response = *dynamic.section,
                            .spice = *spice.section,
                            .golden = *golden.section,
                            .convergence = std::move(convergence_value),
                            .dc_threshold = *dc_threshold,
                            .frequency_threshold = *frequency_threshold,
                            .dynamic_threshold = *dynamic_threshold,
                            .run = {.tool_version = *tool_version,
                                    .sample_rate_hz = *sample_rate,
                                    .oversampling = *oversampling,
                                    .random_seed = static_cast<std::uint64_t>(*random_seed_number)}};
    if (package.status == EvidencePackageStatus::Passed &&
        (package.spice.status != EvidenceSectionStatus::Passed || package.golden.status != EvidenceSectionStatus::Passed ||
         package.convergence.status != EvidenceSectionStatus::Passed)) {
        return Failure(EvidenceChainError::MissingBlockedReference);
    }
    if (package.status == EvidencePackageStatus::SchemaOnly &&
        (package.spice.status == EvidenceSectionStatus::Passed || package.golden.status == EvidenceSectionStatus::Passed)) {
        return Failure(EvidenceChainError::InvalidStatus);
    }
    return {.package = std::move(package), .error = std::nullopt};
}

EvidenceEvaluation EvaluateEvidencePackage(const EvidencePackage& package) noexcept {
    if (package.status == EvidencePackageStatus::SchemaOnly) return EvidenceEvaluation::SchemaOnly;
    if (package.status == EvidencePackageStatus::Blocked) return EvidenceEvaluation::Blocked;
    return package.spice.status == EvidenceSectionStatus::Passed &&
                   package.golden.status == EvidenceSectionStatus::Passed &&
                   package.convergence.status == EvidenceSectionStatus::Passed
        ? EvidenceEvaluation::Passed : EvidenceEvaluation::Blocked;
}

EvidenceComparisonResult CompareEvidenceSamples(std::span<const double> measured,
                                                 std::span<const double> reference,
                                                 double threshold) noexcept {
    if (measured.size() != reference.size() || !std::isfinite(threshold) || threshold < 0.0) {
        return {.max_abs_error = std::numeric_limits<double>::infinity(), .passed = false};
    }
    double maximum = 0.0;
    for (std::size_t index = 0; index < measured.size(); ++index) {
        if (!std::isfinite(measured[index]) || !std::isfinite(reference[index])) {
            return {.max_abs_error = std::numeric_limits<double>::infinity(), .passed = false};
        }
        maximum = std::max(maximum, std::fabs(measured[index] - reference[index]));
    }
    return {.max_abs_error = maximum, .passed = maximum <= threshold};
}

} // namespace solo_rig
