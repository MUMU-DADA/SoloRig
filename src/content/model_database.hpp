#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace solo_rig {

enum class ModelAuditError {
    InvalidJson,
    UnsupportedVersion,
    InvalidSchema,
    MissingField,
    InvalidField,
    InvalidSource,
    InvalidLicense,
    InvalidApplicability,
    DuplicateParameter,
    NonFiniteParameter,
    ParameterOutOfRange,
    UnknownModelKind,
    DuplicateModel,
    MissingModelAuthorization,
    Blocked,
    UnknownModel,
    ModelKindMismatch,
};

enum class ModelKind {
    Diode,
    Zener,
    Bjt,
    Jfet,
    Mosfet,
    Opamp,
    Ota,
    Triode,
    Pentode,
    BeamTetrode,
    VariMu,
    TransformerCore,
    TestMathematical,
};

enum class ModelAvailability {
    Approved,
    Planned,
    Blocked,
};

enum class ModelLicenseReviewStatus {
    Approved,
    Pending,
    Blocked,
    Rejected,
};

struct ModelParameterAudit {
    std::string id;
    double default_value{};
    double minimum{};
    double maximum{};
    std::string unit;
};

struct ModelApplicabilityRange {
    std::string quantity;
    double minimum{};
    double maximum{};
    std::string unit;
};

struct ModelSourceAudit {
    std::string origin;
    std::string version;
    std::string integrity;
};

struct ModelLicenseAudit {
    std::string spdx;
    std::string permission_evidence;
    ModelLicenseReviewStatus review_status{ModelLicenseReviewStatus::Pending};
};

struct ModelAuditRecord {
    std::string model_id;
    std::string device_family;
    std::string part_number;
    ModelKind kind{ModelKind::TestMathematical};
    std::optional<std::string> polarity;
    ModelAvailability availability{ModelAvailability::Blocked};
    std::vector<ModelParameterAudit> parameters;
    ModelSourceAudit source;
    ModelLicenseAudit license;
    std::vector<ModelApplicabilityRange> applicability;
    std::vector<std::string> evidence_refs;
};

struct ModelAuditParseResult {
    std::optional<ModelAuditRecord> model;
    std::optional<ModelAuditError> error;
};

struct ModelLookupResult {
    std::optional<ModelAuditRecord> model;
    std::optional<ModelAuditError> error;
};

struct ModelBinding {
    std::string model_id;
    ModelKind expected_kind{ModelKind::TestMathematical};
    std::optional<std::string> expected_polarity;
};

struct ModelKindContract {
    ModelKind kind{ModelKind::TestMathematical};
    ModelAvailability availability{ModelAvailability::Blocked};
};

class ModelDatabase final {
public:
    // Registration is a non-real-time/Prepare operation. It validates license
    // and kind availability before a record can be visible to Lookup/Bind.
    [[nodiscard]] std::optional<ModelAuditError> Register(ModelAuditRecord model);
    [[nodiscard]] ModelLookupResult Lookup(std::string_view model_id) const;
    [[nodiscard]] ModelLookupResult Bind(const ModelBinding& binding) const;
    [[nodiscard]] const std::vector<ModelAuditRecord>& Records() const noexcept { return records_; }

private:
    friend struct ModelDatabaseBuildResult;
    friend ModelDatabaseBuildResult BuildModelDatabase(std::string_view source);

    std::vector<ModelAuditRecord> records_;
};

struct ModelDatabaseBuildResult {
    std::optional<ModelDatabase> database;
    std::optional<ModelAuditError> error;
};

[[nodiscard]] ModelAuditParseResult ParseModelAuditJson(std::string_view source);
[[nodiscard]] ModelDatabaseBuildResult BuildModelDatabase(std::string_view source);
[[nodiscard]] const char* ModelKindName(ModelKind kind) noexcept;
[[nodiscard]] std::vector<ModelKindContract> ModelKindContracts();

} // namespace solo_rig
