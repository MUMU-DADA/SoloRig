#include "content/evidence_chain.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <string>
#include <array>

namespace {
std::string ReadFixture(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" / "whitebox" / name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open M16 fixture: " + path.string());
    std::ostringstream source;
    source << stream.rdbuf();
    return source.str();
}
}

TEST_CASE("M16 distinguishes schema-only evidence from blocked external references") {
    const auto schema = solo_rig::ParseEvidencePackageJson(ReadFixture("m16_schema_only.json"));
    REQUIRE(schema.error == std::nullopt);
    REQUIRE(schema.package.has_value());
    REQUIRE(schema.package->status == solo_rig::EvidencePackageStatus::SchemaOnly);
    REQUIRE(schema.package->spice.status == solo_rig::EvidenceSectionStatus::Blocked);
    REQUIRE(schema.package->golden.status == solo_rig::EvidenceSectionStatus::Blocked);

    const auto blocked = solo_rig::ParseEvidencePackageJson(ReadFixture("m16_blocked.json"));
    if (blocked.error.has_value()) throw std::runtime_error("blocked parse error=" + std::to_string(static_cast<int>(*blocked.error)));
    REQUIRE(blocked.package->status == solo_rig::EvidencePackageStatus::Blocked);
    REQUIRE(solo_rig::EvaluateEvidencePackage(*blocked.package) == solo_rig::EvidenceEvaluation::Blocked);
}

TEST_CASE("M16 accepts only a self-authored passed mathematical evidence package") {
    const auto parsed = solo_rig::ParseEvidencePackageJson(ReadFixture("m16_test_passed.json"));
    if (parsed.error.has_value()) throw std::runtime_error("passed parse error=" + std::to_string(static_cast<int>(*parsed.error)));
    REQUIRE(parsed.package.has_value());
    REQUIRE(parsed.package->status == solo_rig::EvidencePackageStatus::Passed);
    REQUIRE(solo_rig::EvaluateEvidencePackage(*parsed.package) == solo_rig::EvidenceEvaluation::Passed);
    REQUIRE(parsed.package->convergence.cases.size() == 6U);
    REQUIRE(parsed.package->run.sample_rate_hz == 48000.0);
    REQUIRE(parsed.package->run.oversampling == 4U);
}

TEST_CASE("M16 rejects nonfinite and exceeded error thresholds") {
    const auto nonfinite = solo_rig::ParseEvidencePackageJson(ReadFixture("m16_invalid_nonfinite.json"));
    REQUIRE(nonfinite.error == solo_rig::EvidenceChainError::NonFiniteValue);
    const auto exceeded = solo_rig::ParseEvidencePackageJson(ReadFixture("m16_invalid_threshold.json"));
    REQUIRE(exceeded.error == solo_rig::EvidenceChainError::ThresholdExceeded);
}

TEST_CASE("M16 requires deterministic run metadata and validation cases") {
    auto source = ReadFixture("m16_test_passed.json");
    const std::string hash = "\"inputHash\": \"sha256:test-netlist\"";
    source.replace(source.find(hash), hash.size(), "\"inputHash\": \"\"");
    REQUIRE(solo_rig::ParseEvidencePackageJson(source).error == solo_rig::EvidenceChainError::MissingField);
    source = ReadFixture("m16_test_passed.json");
    const std::string seed = "\"randomSeed\": 0";
    source.replace(source.find(seed), seed.size(), "\"randomSeed\": null");
    const auto invalid_seed = solo_rig::ParseEvidencePackageJson(source);
    if (!invalid_seed.error.has_value()) throw std::runtime_error("missing invalid seed error");
    REQUIRE(*invalid_seed.error == solo_rig::EvidenceChainError::NonFiniteValue);
}

TEST_CASE("M16 comparison helper calculates a deterministic threshold without allocation") {
    const std::array<double, 3> measured{1.0, 2.0, 3.0};
    const std::array<double, 3> reference{1.0, 1.999, 3.001};
    const auto passed = solo_rig::CompareEvidenceSamples(measured, reference, 0.002);
    REQUIRE(passed.passed);
    REQUIRE(passed.max_abs_error > 0.0009);
    REQUIRE(passed.max_abs_error < 0.0011);
    const auto failed = solo_rig::CompareEvidenceSamples(measured, reference, 0.0001);
    REQUIRE(!failed.passed);
    const std::array<double, 2> wrong_size{1.0, 2.0};
    REQUIRE(!solo_rig::CompareEvidenceSamples(measured, wrong_size, 0.1).passed);
}
