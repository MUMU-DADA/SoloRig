#include "content/model_database.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

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

TEST_CASE("M14 protected device contracts remain explicit after later family approvals") {
    const auto contracts = solo_rig::ModelKindContracts();
    for (const auto kind : {solo_rig::ModelKind::VariMu, solo_rig::ModelKind::TransformerCore}) {
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

TEST_CASE("M18-C1 registers only self-authored clean-room diode and Zener families") {
    for (const auto fixture : {"m18c1_cleanroom_diode_model.json", "m18c1_cleanroom_zener_model.json"}) {
        const auto parsed = solo_rig::ParseModelAuditJson(ReadFixture(fixture));
        REQUIRE(parsed.error == std::nullopt);
        REQUIRE(parsed.model.has_value());
        REQUIRE(parsed.model->part_number.find("no-commercial-part") != std::string::npos);
        REQUIRE(parsed.model->source.origin.find("self-authored clean-room") != std::string::npos);
        const auto built = solo_rig::BuildModelDatabase(ReadFixture(fixture));
        REQUIRE(built.error == std::nullopt);
        REQUIRE(built.database.has_value());
        REQUIRE(built.database->Lookup(parsed.model->model_id).model.has_value());
    }

    const auto untrusted = solo_rig::ParseModelAuditJson(ReadFixture("m18c1_untrusted_diode_model.json"));
    REQUIRE(untrusted.model.has_value());
    const auto rejected = solo_rig::BuildModelDatabase(ReadFixture("m18c1_untrusted_diode_model.json"));
    REQUIRE(!rejected.database.has_value());
    REQUIRE(rejected.error == solo_rig::ModelAuditError::MissingModelAuthorization);

    const auto contracts = solo_rig::ModelKindContracts();
    for (const auto kind : {solo_rig::ModelKind::Diode, solo_rig::ModelKind::Zener}) {
        const auto found = std::find_if(contracts.begin(), contracts.end(),
                                        [kind](const auto& contract) { return contract.kind == kind; });
        REQUIRE(found != contracts.end());
        REQUIRE(found->availability == solo_rig::ModelAvailability::Approved);
    }
    for (const auto kind : {solo_rig::ModelKind::VariMu, solo_rig::ModelKind::TransformerCore}) {
        const auto found = std::find_if(contracts.begin(), contracts.end(),
                                        [kind](const auto& contract) { return contract.kind == kind; });
        REQUIRE(found != contracts.end());
        REQUIRE(found->availability == solo_rig::ModelAvailability::Blocked);
    }
}

TEST_CASE("M18-C2 audits and registers only complete authorized clean-room BJT models") {
    for (const auto fixture : {"m18c2_cleanroom_npn_model.json", "m18c2_cleanroom_pnp_model.json"}) {
        const auto parsed = solo_rig::ParseModelAuditJson(ReadFixture(fixture));
        REQUIRE(parsed.error == std::nullopt);
        REQUIRE(parsed.model.has_value());
        REQUIRE(parsed.model->polarity.has_value());
        REQUIRE(parsed.model->part_number.find("no-commercial-part") != std::string::npos);
        const auto built = solo_rig::BuildModelDatabase(ReadFixture(fixture));
        REQUIRE(built.database.has_value());
        REQUIRE(built.error == std::nullopt);
        const auto bound = built.database->Bind({parsed.model->model_id, solo_rig::ModelKind::Bjt,
                                                  parsed.model->polarity});
        REQUIRE(bound.model.has_value());
        const auto wrong_polarity = *parsed.model->polarity == "npn" ? "pnp" : "npn";
        REQUIRE(built.database->Bind({parsed.model->model_id, solo_rig::ModelKind::Bjt, wrong_polarity}).error ==
                solo_rig::ModelAuditError::ModelKindMismatch);
    }
    const auto untrusted = solo_rig::BuildModelDatabase(ReadFixture("m18c2_untrusted_bjt_model.json"));
    REQUIRE(!untrusted.database.has_value());
    REQUIRE(untrusted.error == solo_rig::ModelAuditError::MissingModelAuthorization);
    const auto incomplete = solo_rig::BuildModelDatabase(ReadFixture("m18c2_incomplete_bjt_model.json"));
    REQUIRE(!incomplete.database.has_value());
    REQUIRE(incomplete.error == solo_rig::ModelAuditError::MissingModelAuthorization);

    const auto parsed = solo_rig::ParseModelAuditJson(ReadFixture("m18c2_cleanroom_npn_model.json"));
    REQUIRE(parsed.model.has_value());
    auto invalid_polarity = *parsed.model;
    invalid_polarity.polarity = "invalid";
    solo_rig::ModelDatabase invalid_polarity_database;
    REQUIRE(invalid_polarity_database.Register(std::move(invalid_polarity)) ==
            solo_rig::ModelAuditError::MissingModelAuthorization);

    auto spoofed_commercial_part = *parsed.model;
    spoofed_commercial_part.part_number = "commercial-device-no-commercial-part";
    solo_rig::ModelDatabase spoofed_commercial_database;
    REQUIRE(spoofed_commercial_database.Register(std::move(spoofed_commercial_part)) ==
            solo_rig::ModelAuditError::MissingModelAuthorization);

    auto unsafe_default = *parsed.model;
    const auto max_step = std::find_if(unsafe_default.parameters.begin(), unsafe_default.parameters.end(),
                                       [](const auto& parameter) { return parameter.id == "max-step"; });
    REQUIRE(max_step != unsafe_default.parameters.end());
    max_step->default_value = 20.0;
    max_step->maximum = 20.0;
    solo_rig::ModelDatabase unsafe_default_database;
    REQUIRE(unsafe_default_database.Register(std::move(unsafe_default)) ==
            solo_rig::ModelAuditError::MissingModelAuthorization);

    auto unsafe_range = *parsed.model;
    const auto saturation = std::find_if(unsafe_range.parameters.begin(), unsafe_range.parameters.end(),
                                         [](const auto& parameter) { return parameter.id == "saturation-current"; });
    REQUIRE(saturation != unsafe_range.parameters.end());
    saturation->minimum = 0.0;
    solo_rig::ModelDatabase unsafe_range_database;
    REQUIRE(unsafe_range_database.Register(std::move(unsafe_range)) ==
            solo_rig::ModelAuditError::MissingModelAuthorization);
    const auto contracts = solo_rig::ModelKindContracts();
    const auto bjt = std::find_if(contracts.begin(), contracts.end(), [](const auto& contract) {
        return contract.kind == solo_rig::ModelKind::Bjt;
    });
    REQUIRE(bjt != contracts.end());
    REQUIRE(bjt->availability == solo_rig::ModelAvailability::Approved);
}

TEST_CASE("M18-C3 audits and registers only complete authorized clean-room JFET models") {
    for (const auto fixture : {"m18c3_cleanroom_n_jfet_model.json", "m18c3_cleanroom_p_jfet_model.json"}) {
        const auto parsed = solo_rig::ParseModelAuditJson(ReadFixture(fixture));
        REQUIRE(parsed.error == std::nullopt);
        REQUIRE(parsed.model.has_value());
        REQUIRE(parsed.model->polarity.has_value());
        REQUIRE(parsed.model->part_number == "self-authored-" + *parsed.model->polarity + "-no-commercial-part");
        const auto built = solo_rig::BuildModelDatabase(ReadFixture(fixture));
        REQUIRE(built.database.has_value());
        REQUIRE(built.error == std::nullopt);
        REQUIRE(built.database->Bind({parsed.model->model_id, solo_rig::ModelKind::Jfet,
                                      parsed.model->polarity}).model.has_value());
        const auto wrong_polarity = *parsed.model->polarity == "n-channel" ? "p-channel" : "n-channel";
        REQUIRE(built.database->Bind({parsed.model->model_id, solo_rig::ModelKind::Jfet, wrong_polarity}).error ==
                solo_rig::ModelAuditError::ModelKindMismatch);
    }
    REQUIRE(solo_rig::BuildModelDatabase(ReadFixture("m18c3_untrusted_jfet_model.json")).error ==
            solo_rig::ModelAuditError::MissingModelAuthorization);
    REQUIRE(solo_rig::BuildModelDatabase(ReadFixture("m18c3_incomplete_jfet_model.json")).error ==
            solo_rig::ModelAuditError::MissingModelAuthorization);

    const auto parsed = solo_rig::ParseModelAuditJson(ReadFixture("m18c3_cleanroom_n_jfet_model.json"));
    REQUIRE(parsed.model.has_value());
    auto invalid_polarity = *parsed.model;
    invalid_polarity.polarity = "invalid";
    solo_rig::ModelDatabase invalid_polarity_database;
    REQUIRE(invalid_polarity_database.Register(std::move(invalid_polarity)) ==
            solo_rig::ModelAuditError::MissingModelAuthorization);

    auto spoofed_part = *parsed.model;
    spoofed_part.part_number = "commercial-jfet-no-commercial-part";
    solo_rig::ModelDatabase spoofed_database;
    REQUIRE(spoofed_database.Register(std::move(spoofed_part)) ==
            solo_rig::ModelAuditError::MissingModelAuthorization);

    auto unsafe_range = *parsed.model;
    const auto parameter = std::find_if(unsafe_range.parameters.begin(), unsafe_range.parameters.end(),
                                        [](const auto& item) { return item.id == "pinch-off-voltage"; });
    REQUIRE(parameter != unsafe_range.parameters.end());
    parameter->maximum = 101.0;
    solo_rig::ModelDatabase unsafe_database;
    REQUIRE(unsafe_database.Register(std::move(unsafe_range)) ==
            solo_rig::ModelAuditError::MissingModelAuthorization);

    const auto contracts = solo_rig::ModelKindContracts();
    const auto jfet = std::find_if(contracts.begin(), contracts.end(), [](const auto& contract) {
        return contract.kind == solo_rig::ModelKind::Jfet;
    });
    REQUIRE(jfet != contracts.end());
    REQUIRE(jfet->availability == solo_rig::ModelAvailability::Approved);
}

TEST_CASE("M18-C4 audits and registers only complete authorized clean-room MOSFET models") {
    for (const auto fixture : {"m18c4_cleanroom_n_mosfet_model.json", "m18c4_cleanroom_p_mosfet_model.json"}) {
        const auto parsed = solo_rig::ParseModelAuditJson(ReadFixture(fixture));
        REQUIRE(parsed.error == std::nullopt);
        REQUIRE(parsed.model.has_value());
        REQUIRE(parsed.model->polarity.has_value());
        REQUIRE(parsed.model->part_number == "self-authored-" + *parsed.model->polarity + "-no-commercial-part");
        const auto built = solo_rig::BuildModelDatabase(ReadFixture(fixture));
        REQUIRE(built.database.has_value());
        REQUIRE(built.error == std::nullopt);
        REQUIRE(built.database->Bind({parsed.model->model_id, solo_rig::ModelKind::Mosfet,
                                      parsed.model->polarity}).model.has_value());
        const auto wrong_polarity = *parsed.model->polarity == "n-channel" ? "p-channel" : "n-channel";
        REQUIRE(built.database->Bind({parsed.model->model_id, solo_rig::ModelKind::Mosfet, wrong_polarity}).error ==
                solo_rig::ModelAuditError::ModelKindMismatch);
    }
    REQUIRE(solo_rig::BuildModelDatabase(ReadFixture("m18c4_untrusted_mosfet_model.json")).error ==
            solo_rig::ModelAuditError::MissingModelAuthorization);
    REQUIRE(solo_rig::BuildModelDatabase(ReadFixture("m18c4_incomplete_mosfet_model.json")).error ==
            solo_rig::ModelAuditError::MissingModelAuthorization);

    const auto parsed = solo_rig::ParseModelAuditJson(ReadFixture("m18c4_cleanroom_n_mosfet_model.json"));
    REQUIRE(parsed.model.has_value());
    auto invalid_polarity = *parsed.model;
    invalid_polarity.polarity = "invalid";
    solo_rig::ModelDatabase invalid_database;
    REQUIRE(invalid_database.Register(std::move(invalid_polarity)) ==
            solo_rig::ModelAuditError::MissingModelAuthorization);

    auto spoofed_part = *parsed.model;
    spoofed_part.part_number = "commercial-mosfet-no-commercial-part";
    solo_rig::ModelDatabase spoofed_database;
    REQUIRE(spoofed_database.Register(std::move(spoofed_part)) ==
            solo_rig::ModelAuditError::MissingModelAuthorization);

    auto unsafe_range = *parsed.model;
    const auto threshold = std::find_if(unsafe_range.parameters.begin(), unsafe_range.parameters.end(),
                                        [](const auto& item) { return item.id == "threshold-voltage"; });
    REQUIRE(threshold != unsafe_range.parameters.end());
    threshold->maximum = 101.0;
    solo_rig::ModelDatabase unsafe_database;
    REQUIRE(unsafe_database.Register(std::move(unsafe_range)) ==
            solo_rig::ModelAuditError::MissingModelAuthorization);

    const auto contracts = solo_rig::ModelKindContracts();
    const auto mosfet = std::find_if(contracts.begin(), contracts.end(), [](const auto& contract) {
        return contract.kind == solo_rig::ModelKind::Mosfet;
    });
    REQUIRE(mosfet != contracts.end());
    REQUIRE(mosfet->availability == solo_rig::ModelAvailability::Approved);
}

TEST_CASE("M18-C5 audits and registers only complete authorized clean-room opamp and OTA models") {
    for (const auto& fixture : std::array{
             std::pair{"m18c5_cleanroom_opamp_model.json", solo_rig::ModelKind::Opamp},
             std::pair{"m18c5_cleanroom_ota_model.json", solo_rig::ModelKind::Ota}}) {
        const auto built = solo_rig::BuildModelDatabase(ReadFixture(fixture.first));
        REQUIRE(built.database.has_value());
        REQUIRE(built.database->Bind({built.database->Records().front().model_id, fixture.second}).model.has_value());
    }
    REQUIRE(solo_rig::BuildModelDatabase(ReadFixture("m18c5_untrusted_opamp_model.json")).error ==
            solo_rig::ModelAuditError::MissingModelAuthorization);
    REQUIRE(solo_rig::BuildModelDatabase(ReadFixture("m18c5_incomplete_ota_model.json")).error ==
            solo_rig::ModelAuditError::MissingModelAuthorization);

    const auto parsed = solo_rig::ParseModelAuditJson(ReadFixture("m18c5_cleanroom_opamp_model.json"));
    REQUIRE(parsed.model.has_value());
    auto commercial_part = *parsed.model;
    commercial_part.part_number = "commercial-opamp-no-commercial-part";
    solo_rig::ModelDatabase rejected;
    REQUIRE(rejected.Register(std::move(commercial_part)) ==
            solo_rig::ModelAuditError::MissingModelAuthorization);

    auto reversed_limits = *parsed.model;
    const auto minimum = std::find_if(reversed_limits.parameters.begin(), reversed_limits.parameters.end(),
                                      [](const auto& item) { return item.id == "minimum-output"; });
    const auto maximum = std::find_if(reversed_limits.parameters.begin(), reversed_limits.parameters.end(),
                                      [](const auto& item) { return item.id == "maximum-output"; });
    REQUIRE(minimum != reversed_limits.parameters.end());
    REQUIRE(maximum != reversed_limits.parameters.end());
    minimum->default_value = 3.0;
    maximum->default_value = 2.0;
    solo_rig::ModelDatabase reversed;
    REQUIRE(reversed.Register(std::move(reversed_limits)) ==
            solo_rig::ModelAuditError::MissingModelAuthorization);

    auto wrong_unit = *parsed.model;
    const auto gain = std::find_if(wrong_unit.parameters.begin(), wrong_unit.parameters.end(),
                                   [](const auto& item) { return item.id == "open-loop-gain"; });
    REQUIRE(gain != wrong_unit.parameters.end());
    gain->unit = "V/V?";
    solo_rig::ModelDatabase wrong_unit_database;
    REQUIRE(wrong_unit_database.Register(std::move(wrong_unit)) ==
            solo_rig::ModelAuditError::MissingModelAuthorization);

    const auto contracts = solo_rig::ModelKindContracts();
    for (const auto kind : {solo_rig::ModelKind::Opamp, solo_rig::ModelKind::Ota}) {
        const auto found = std::find_if(contracts.begin(), contracts.end(), [kind](const auto& contract) {
            return contract.kind == kind;
        });
        REQUIRE(found != contracts.end());
        REQUIRE(found->availability == solo_rig::ModelAvailability::Approved);
    }
}

TEST_CASE("M18-C6 audits and registers only complete authorized clean-room triode models") {
    const auto built = solo_rig::BuildModelDatabase(ReadFixture("m18c6_cleanroom_triode_model.json"));
    REQUIRE(built.database.has_value());
    REQUIRE(built.database->Bind({"solorig.cleanroom.triode.space-charge.v1",
                                  solo_rig::ModelKind::Triode}).model.has_value());
    REQUIRE(built.database->Bind({"solorig.cleanroom.triode.space-charge.v1",
                                  solo_rig::ModelKind::Pentode}).error ==
            solo_rig::ModelAuditError::ModelKindMismatch);
    REQUIRE(solo_rig::BuildModelDatabase(ReadFixture("m18c6_untrusted_triode_model.json")).error ==
            solo_rig::ModelAuditError::MissingModelAuthorization);
    REQUIRE(solo_rig::BuildModelDatabase(ReadFixture("m18c6_incomplete_triode_model.json")).error ==
            solo_rig::ModelAuditError::MissingModelAuthorization);

    const auto parsed = solo_rig::ParseModelAuditJson(ReadFixture("m18c6_cleanroom_triode_model.json"));
    REQUIRE(parsed.model.has_value());
    auto commercial = *parsed.model;
    commercial.part_number = "commercial-tube-no-commercial-part";
    solo_rig::ModelDatabase commercial_database;
    REQUIRE(commercial_database.Register(std::move(commercial)) ==
            solo_rig::ModelAuditError::MissingModelAuthorization);
    auto wrong_unit = *parsed.model;
    const auto perveance = std::find_if(wrong_unit.parameters.begin(), wrong_unit.parameters.end(),
                                        [](const auto& item) { return item.id == "perveance"; });
    REQUIRE(perveance != wrong_unit.parameters.end());
    perveance->unit = "A/V";
    solo_rig::ModelDatabase wrong_unit_database;
    REQUIRE(wrong_unit_database.Register(std::move(wrong_unit)) ==
            solo_rig::ModelAuditError::MissingModelAuthorization);
    const auto contracts = solo_rig::ModelKindContracts();
    const auto triode = std::find_if(contracts.begin(), contracts.end(), [](const auto& contract) {
        return contract.kind == solo_rig::ModelKind::Triode;
    });
    REQUIRE(triode != contracts.end());
    REQUIRE(triode->availability == solo_rig::ModelAvailability::Approved);
}

TEST_CASE("M18-C7a audits and registers only complete authorized clean-room pentode models") {
    const auto built = solo_rig::BuildModelDatabase(ReadFixture("m18c7a_cleanroom_pentode_model.json"));
    REQUIRE(built.database.has_value());
    REQUIRE(built.database->Bind({"solorig.cleanroom.pentode.space-charge.v1",
                                  solo_rig::ModelKind::Pentode}).model.has_value());
    REQUIRE(solo_rig::BuildModelDatabase(ReadFixture("m18c7a_untrusted_pentode_model.json")).error ==
            solo_rig::ModelAuditError::MissingModelAuthorization);
    REQUIRE(solo_rig::BuildModelDatabase(ReadFixture("m18c7a_incomplete_pentode_model.json")).error ==
            solo_rig::ModelAuditError::MissingModelAuthorization);
    const auto contracts = solo_rig::ModelKindContracts();
    const auto pentode = std::find_if(contracts.begin(), contracts.end(), [](const auto& item) {
        return item.kind == solo_rig::ModelKind::Pentode;
    });
    REQUIRE(pentode != contracts.end());
    REQUIRE(pentode->availability == solo_rig::ModelAvailability::Approved);
}
