#include "content/circuit_v2.hpp"
#include "content/dynamic_speaker_load_binding.hpp"
#include "content/model_database.hpp"
#include "dsp/mna_solver.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

std::string ReadFixture(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" / "whitebox" / name;
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("M18-C10b audited speaker model binds the same runtime POD") {
    const auto database = solo_rig::BuildModelDatabase(
        ReadFixture("m18c10b_cleanroom_speaker_model.json"));
    REQUIRE(database.database.has_value());
    const auto record = database.database->Bind({
        "solorig.cleanroom.speaker-load.electrodynamic.v1", solo_rig::ModelKind::SpeakerLoad});
    REQUIRE(record.model.has_value());
    solo_rig::CircuitSpeakerLoadModel bound;
    REQUIRE(solo_rig::BindAuditedSpeakerLoadModel(*record.model, bound));
    REQUIRE(bound.kind == "electrodynamic");
    REQUIRE(bound.voice_coil_resistance == 6.0);
    REQUIRE(bound.force_factor == 5.0);
    REQUIRE(bound.initial_displacement == 0.0);
}

TEST_CASE("M18-C10b audited speaker reference reaches Prepare and rejects missing authorization") {
    const auto parsed = solo_rig::ParseCircuitV2Json(
        ReadFixture("m18c10b_dynamic_speaker_reference.json"));
    const auto database = solo_rig::BuildModelDatabase(
        ReadFixture("m18c10b_cleanroom_speaker_model.json"));
    REQUIRE(parsed.circuit.has_value());
    REQUIRE(database.database.has_value());
    REQUIRE(solo_rig::CompileMnaCircuit(*parsed.circuit).error ==
            solo_rig::MnaCompileError::MissingModelAuthorization);
    auto compiled = solo_rig::CompileMnaCircuit(*parsed.circuit, *database.database);
    REQUIRE(compiled.circuit.has_value());
    REQUIRE(compiled.circuit->Prepare(192000.0));
    REQUIRE(compiled.circuit->Solve(0.5).Succeeded());

    const auto wrong = database.database->Bind({
        "solorig.cleanroom.speaker-load.electrodynamic.v1", solo_rig::ModelKind::Mosfet});
    REQUIRE(!wrong.model.has_value());
    REQUIRE(wrong.error == solo_rig::ModelAuditError::ModelKindMismatch);
}

TEST_CASE("M18-C10b speaker audit rejects an incomplete parameter contract") {
    auto source = ReadFixture("m18c10b_cleanroom_speaker_model.json");
    const auto position = source.find("{\"id\":\"force-factor\"");
    REQUIRE(position != std::string::npos);
    const auto end = source.find("},", position);
    REQUIRE(end != std::string::npos);
    source.erase(position, end + 2U - position);
    const auto rejected = solo_rig::BuildModelDatabase(source);
    REQUIRE(!rejected.database.has_value());
    REQUIRE(rejected.error == solo_rig::ModelAuditError::MissingModelAuthorization);
}
