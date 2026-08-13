#include "content/model_database.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {

std::string ReadFixture(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" / "whitebox" / name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open M14 fixture: " + path.string());
    }
    std::ostringstream source;
    source << stream.rdbuf();
    return source.str();
}

} // namespace

TEST_CASE("M14 parses an approved clean-room mathematical audit record") {
    const auto parsed = solo_rig::ParseModelAuditJson(ReadFixture("m14_test_math_model.json"));
    REQUIRE(parsed.error == std::nullopt);
    REQUIRE(parsed.model.has_value());
    REQUIRE(parsed.model->model_id == "test.math.cubic.v1");
    REQUIRE(parsed.model->kind == solo_rig::ModelKind::TestMathematical);
    REQUIRE(parsed.model->parameters.size() == 1U);
    REQUIRE(parsed.model->parameters[0].default_value == 1.0);
    REQUIRE(parsed.model->license.review_status == solo_rig::ModelLicenseReviewStatus::Approved);
}

TEST_CASE("M14 builds a database and binds by model id and expected kind") {
    const auto built = solo_rig::BuildModelDatabase(ReadFixture("m14_test_math_model.json"));
    REQUIRE(built.error == std::nullopt);
    REQUIRE(built.database.has_value());
    const auto lookup = built.database->Lookup("test.math.cubic.v1");
    REQUIRE(lookup.error == std::nullopt);
    REQUIRE(lookup.model.has_value());
    const auto binding = built.database->Bind({"test.math.cubic.v1", solo_rig::ModelKind::TestMathematical});
    REQUIRE(binding.error == std::nullopt);
    REQUIRE(binding.model.has_value());
    const auto mismatch = built.database->Bind({"test.math.cubic.v1", solo_rig::ModelKind::Diode});
    REQUIRE(mismatch.error == solo_rig::ModelAuditError::ModelKindMismatch);
    const auto unknown = built.database->Lookup("missing.model");
    REQUIRE(unknown.error == solo_rig::ModelAuditError::UnknownModel);

    solo_rig::ModelDatabase registered;
    const auto parsed = solo_rig::ParseModelAuditJson(ReadFixture("m14_test_math_model.json"));
    REQUIRE(parsed.model.has_value());
    REQUIRE(registered.Register(*parsed.model) == std::nullopt);
    REQUIRE(registered.Register(*parsed.model) == solo_rig::ModelAuditError::DuplicateModel);
}

TEST_CASE("M14 blocks pending authorization before a runtime binding can be created") {
    const auto parsed = solo_rig::ParseModelAuditJson(ReadFixture("m14_blocked_model.json"));
    REQUIRE(parsed.error == std::nullopt);
    REQUIRE(parsed.model.has_value());
    REQUIRE(parsed.model->license.review_status == solo_rig::ModelLicenseReviewStatus::Pending);
    const auto built = solo_rig::BuildModelDatabase(ReadFixture("m14_blocked_model.json"));
    REQUIRE(built.error == solo_rig::ModelAuditError::MissingModelAuthorization);
    REQUIRE(!built.database.has_value());

    auto blocked_license = ReadFixture("m14_blocked_model.json");
    const std::string pending_status = "\"reviewStatus\": \"pending\"";
    blocked_license.replace(blocked_license.find(pending_status), pending_status.size(),
                           "\"reviewStatus\": \"blocked\"");
    const auto blocked_build = solo_rig::BuildModelDatabase(blocked_license);
    REQUIRE(blocked_build.error == solo_rig::ModelAuditError::MissingModelAuthorization);
}

TEST_CASE("M14 rejects missing fields, nonfinite values, ranges, version and unknown kinds") {
    REQUIRE(solo_rig::ParseModelAuditJson(ReadFixture("m14_missing_field.json")).error ==
            solo_rig::ModelAuditError::MissingField);
    REQUIRE(solo_rig::ParseModelAuditJson(ReadFixture("m14_nonfinite_parameter.json")).error ==
            solo_rig::ModelAuditError::NonFiniteParameter);
    REQUIRE(solo_rig::ParseModelAuditJson(ReadFixture("m14_out_of_range_parameter.json")).error ==
            solo_rig::ModelAuditError::ParameterOutOfRange);
    REQUIRE(solo_rig::ParseModelAuditJson(ReadFixture("m14_wrong_version.json")).error ==
            solo_rig::ModelAuditError::UnsupportedVersion);
    REQUIRE(solo_rig::ParseModelAuditJson(ReadFixture("m14_unknown_kind.json")).error ==
            solo_rig::ModelAuditError::UnknownModelKind);
}

TEST_CASE("M14 exposes explicit blocked contracts for every protected device family") {
    const auto contracts = solo_rig::ModelKindContracts();
    for (const auto kind : {solo_rig::ModelKind::Diode, solo_rig::ModelKind::Zener,
                            solo_rig::ModelKind::Bjt, solo_rig::ModelKind::Jfet,
                            solo_rig::ModelKind::Mosfet, solo_rig::ModelKind::Opamp,
                            solo_rig::ModelKind::Ota, solo_rig::ModelKind::Triode,
                            solo_rig::ModelKind::Pentode, solo_rig::ModelKind::VariMu,
                            solo_rig::ModelKind::TransformerCore}) {
        const auto found = std::find_if(contracts.begin(), contracts.end(),
                                        [kind](const auto& contract) { return contract.kind == kind; });
        REQUIRE(found != contracts.end());
        REQUIRE(found->availability == solo_rig::ModelAvailability::Blocked);
    }
    const auto test_kind = std::find_if(contracts.begin(), contracts.end(),
                                        [](const auto& contract) {
                                            return contract.kind == solo_rig::ModelKind::TestMathematical;
                                        });
    REQUIRE(test_kind != contracts.end());
    REQUIRE(test_kind->availability == solo_rig::ModelAvailability::Approved);
}
