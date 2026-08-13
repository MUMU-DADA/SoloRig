#include "content/whitebox_compiler.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {

std::string ReadFixture(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" / "whitebox" / name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open M12 fixture: " + path.string());
    }
    std::ostringstream source;
    source << stream.rdbuf();
    return source.str();
}

} // namespace

TEST_CASE("M12 compiler builds immutable series topology IR") {
    const auto result = solo_rig::CompileWhiteboxGraphV2Json(ReadFixture("m12_series.json"));
    REQUIRE(result.error == std::nullopt);
    REQUIRE(result.graph.has_value());
    REQUIRE(result.classification == solo_rig::WhiteboxTopologyClass::Series);
    REQUIRE(result.graph->Nodes().size() == 4U);
    REQUIRE(result.graph->Components().size() == 2U);
    REQUIRE(result.graph->Ports().size() == 2U);
    REQUIRE(result.graph->Backend() == solo_rig::WhiteboxBackend::Mna);
    auto prepared = result.graph->Prepare();
    REQUIRE(prepared.error == std::nullopt);
    REQUIRE(prepared.graph.has_value());
    REQUIRE(prepared.graph->IsPrepared());
    double output{};
    REQUIRE(prepared.graph->Process(0.25, output) == solo_rig::WhiteboxProcessError::UnsupportedRuntime);
}

TEST_CASE("M12 compiler builds immutable parallel topology IR and accepts WDF selection") {
    const auto result = solo_rig::CompileWhiteboxGraphV2Json(ReadFixture("m12_parallel.json"));
    REQUIRE(result.error == std::nullopt);
    REQUIRE(result.graph.has_value());
    REQUIRE(result.classification == solo_rig::WhiteboxTopologyClass::Parallel);
    REQUIRE(result.graph->Backend() == solo_rig::WhiteboxBackend::Wdf);
    REQUIRE(result.graph->Prepare().graph.has_value());
}

TEST_CASE("M18-B compiler preserves validated SPQR and rigid IR for topology lowering") {
    const auto spqr = solo_rig::CompileWhiteboxGraphV2Json(ReadFixture("m12_spqr_pending.json"));
    REQUIRE(spqr.classification == solo_rig::WhiteboxTopologyClass::Spqr);
    REQUIRE(spqr.error == std::nullopt);
    REQUIRE(spqr.graph.has_value());
    REQUIRE(spqr.graph->Prepare().error == solo_rig::WhiteboxCompileError::UnsupportedTopology);

    const auto rigid = solo_rig::CompileWhiteboxGraphV2Json(ReadFixture("m12_rigid_pending.json"));
    REQUIRE(rigid.classification == solo_rig::WhiteboxTopologyClass::Rigid);
    REQUIRE(rigid.error == std::nullopt);
    REQUIRE(rigid.graph.has_value());
    REQUIRE(rigid.graph->Prepare().error == solo_rig::WhiteboxCompileError::UnsupportedTopology);
}

TEST_CASE("M12 compiler rejects duplicate IDs, dangling ports and unknown backends atomically") {
    const auto result = solo_rig::CompileWhiteboxGraphV2Json(ReadFixture("m12_invalid_graph.json"));
    REQUIRE(!result.graph.has_value());
    REQUIRE(result.error == solo_rig::WhiteboxCompileError::DuplicateId);

    auto unknown_backend = ReadFixture("m12_series.json");
    const auto backend_text = std::string("\"preferred\": \"mna\"");
    unknown_backend.replace(unknown_backend.find(backend_text), backend_text.size(),
                            "\"preferred\": \"unknown-backend\"");
    const auto backend_result = solo_rig::CompileWhiteboxGraphV2Json(unknown_backend);
    REQUIRE(backend_result.error == solo_rig::WhiteboxCompileError::InvalidBackend);
}

TEST_CASE("M12 compiler rejects unsafe model paths before constructing an IR") {
    const auto result = solo_rig::CompileWhiteboxGraphV2Json(ReadFixture("m12_unsafe_model.json"));
    REQUIRE(!result.graph.has_value());
    REQUIRE(result.error == solo_rig::WhiteboxCompileError::UnsafeModelPath);
}

TEST_CASE("M12 compiler rejects version, schema and missing model references deterministically") {
    const auto base = ReadFixture("m12_series.json");
    auto bad_version = base;
    bad_version.replace(bad_version.find("\"version\": 2"), std::string("\"version\": 2").size(),
                        "\"version\": 3");
    const auto version_result = solo_rig::CompileWhiteboxGraphV2Json(bad_version);
    REQUIRE(version_result.error == solo_rig::WhiteboxCompileError::UnsupportedVersion);

    auto bad_schema = base;
    bad_schema.replace(bad_schema.find("\"schema\": \"circuit\""), std::string("\"schema\": \"circuit\"").size(),
                       "\"schema\": \"other\"");
    const auto schema_result = solo_rig::CompileWhiteboxGraphV2Json(bad_schema);
    REQUIRE(schema_result.error == solo_rig::WhiteboxCompileError::InvalidSchema);

    auto missing_model = base;
    const auto pin_end = missing_model.find("\"pins\": [\"in\", \"mid\"]");
    missing_model.insert(pin_end + std::string("\"pins\": [\"in\", \"mid\"]").size(),
                         ", \"modelReference\": \"missing\"");
    const auto missing_result = solo_rig::CompileWhiteboxGraphV2Json(missing_model);
    REQUIRE(missing_result.error == solo_rig::WhiteboxCompileError::MissingModelReference);

    const auto subcircuit = solo_rig::CompileWhiteboxGraphV2Json(ReadFixture("m12_subcircuit_missing_model.json"));
    REQUIRE(subcircuit.error == solo_rig::WhiteboxCompileError::MissingModelReference);
}

TEST_CASE("M12 prepared graph exposes a non-allocating process boundary with stable refusal") {
    const auto result = solo_rig::CompileWhiteboxGraphV2Json(ReadFixture("m12_series.json"));
    REQUIRE(result.graph.has_value());
    auto prepared = result.graph->Prepare();
    REQUIRE(prepared.graph.has_value());
    double output{};
    REQUIRE(prepared.graph->Process(std::numeric_limits<double>::quiet_NaN(), output) ==
            solo_rig::WhiteboxProcessError::NonFiniteInput);
    REQUIRE(prepared.graph->Process(0.0, output) == solo_rig::WhiteboxProcessError::UnsupportedRuntime);
}
