#include "content/json.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

solo_rig::JsonValue FixtureValue(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" / "whitebox" / name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open white-box contract fixture: " + path.string());
    }
    std::ostringstream source;
    source << stream.rdbuf();
    const auto parsed = solo_rig::ParseJson(source.str());
    if (!parsed.value.has_value()) {
        throw std::runtime_error("invalid white-box contract fixture: " + path.string());
    }
    return *parsed.value;
}

const solo_rig::JsonValue& Required(const solo_rig::JsonValue::Object& object, std::string_view key) {
    const auto iterator = object.find(key);
    if (iterator == object.end()) {
        throw std::runtime_error("missing contract field: " + std::string(key));
    }
    return iterator->second;
}

bool ArrayContains(const solo_rig::JsonValue& value, std::string_view expected) {
    const auto* array = value.AsArray();
    if (array == nullptr) {
        return false;
    }
    for (const auto& item : *array) {
        if (item.AsString() != nullptr && *item.AsString() == expected) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("M11 white-box baseline enumerates graph, backend and solver contracts") {
    const auto fixture = FixtureValue("whitebox_contract_baseline.json");
    const auto* object = fixture.AsObject();
    REQUIRE(object != nullptr);
    REQUIRE(*Required(*object, "implementationStatus").AsString() == "contract-only");
    const auto* graph = Required(*object, "graph").AsObject();
    REQUIRE(graph != nullptr);
    for (const auto field : {"nodes", "pins", "ports", "supplies", "subcircuits", "modelReferences"}) {
        REQUIRE(ArrayContains(Required(*graph, "required"), field));
    }
    const auto* topology = Required(*object, "topology").AsObject();
    REQUIRE(topology != nullptr);
    for (const auto class_name : {"series", "parallel", "spqr", "rigid"}) {
        REQUIRE(ArrayContains(Required(*topology, "classifications"), class_name));
    }
    const auto* backends = Required(*object, "backends").AsObject();
    REQUIRE(backends != nullptr);
    REQUIRE(*Required(*Required(*backends, "mna").AsObject(), "status").AsString() == "planned-contract");
    REQUIRE(*Required(*Required(*backends, "wdf").AsObject(), "status").AsString() == "planned-contract");
    const auto* nonlinear = Required(*object, "nonlinearSolve").AsObject();
    REQUIRE(nonlinear != nullptr);
    REQUIRE(ArrayContains(Required(*nonlinear, "groups"), "joint-device-group"));
    REQUIRE(ArrayContains(Required(*nonlinear, "groups"), "feedback-group"));
}

TEST_CASE("M11 graph fixture records generic inputs without claiming a compiler") {
    const auto fixture = FixtureValue("graph_valid.contract.json");
    const auto* object = fixture.AsObject();
    REQUIRE(object != nullptr);
    REQUIRE(*Required(*object, "implementationStatus").AsString() == "contract-only");
    REQUIRE(Required(*object, "nodes").AsArray() != nullptr);
    REQUIRE(Required(*object, "ports").AsArray() != nullptr);
    REQUIRE(Required(*object, "supplies").AsArray() != nullptr);
    REQUIRE(Required(*object, "subcircuits").AsArray() != nullptr);
    REQUIRE(Required(*object, "modelReferences").AsArray() != nullptr);
    REQUIRE(*Required(*object, "expectedResult").AsString() == "pending-compiler-implementation");
}

TEST_CASE("M11 unsafe model fixture requires rejection and no partial runtime") {
    const auto fixture = FixtureValue("graph_invalid_unsafe_model.contract.json");
    const auto* object = fixture.AsObject();
    REQUIRE(object != nullptr);
    REQUIRE(*Required(*object, "implementationStatus").AsString() == "contract-only");
    REQUIRE(ArrayContains(Required(*object, "expectedErrors"), "UnsafeAssetPath"));
    REQUIRE(ArrayContains(Required(*object, "expectedErrors"), "MissingModelAuthorization"));
    REQUIRE(*Required(*object, "expectedResult").AsString() == "reject-without-partial-runtime");
}

TEST_CASE("M11 model and module evidence templates stay blocked until authorized data exists") {
    const auto model_fixture = FixtureValue("model_audit.template.json");
    const auto* model = model_fixture.AsObject();
    REQUIRE(model != nullptr);
    const auto* license = Required(*model, "license").AsObject();
    REQUIRE(license != nullptr);
    REQUIRE(*Required(*license, "reviewStatus").AsString() == "blocked");
    REQUIRE(Required(*model, "parameters").AsArray() != nullptr);
    REQUIRE(Required(*model, "evidenceRefs").AsArray() != nullptr);

    const auto evidence_fixture = FixtureValue("module_evidence.template.json");
    const auto* evidence = evidence_fixture.AsObject();
    REQUIRE(evidence != nullptr);
    REQUIRE(*Required(*evidence, "status").AsString() == "template-only");
    const auto* spice = Required(*evidence, "spiceComparison").AsObject();
    const auto* golden = Required(*evidence, "goldenComparison").AsObject();
    REQUIRE(*Required(*spice, "status").AsString() == "blocked-no-authorized-reference");
    REQUIRE(*Required(*golden, "status").AsString() == "blocked-no-golden-data");
}

TEST_CASE("M11 real-time boundary is a required contract, not an implementation claim") {
    const auto fixture = FixtureValue("whitebox_contract_baseline.json");
    const auto* object = fixture.AsObject();
    REQUIRE(object != nullptr);
    const auto* boundary = Required(*object, "realtimeBoundary").AsObject();
    REQUIRE(boundary != nullptr);
    REQUIRE(*Required(*boundary, "processReadsFiles").AsBoolean() == false);
    REQUIRE(*Required(*boundary, "processParsesJson").AsBoolean() == false);
    REQUIRE(*Required(*boundary, "processAllocates").AsBoolean() == false);
    REQUIRE(*Required(*boundary, "processLocks").AsBoolean() == false);
    REQUIRE(*Required(*boundary, "prepareOwnsCompilation").AsBoolean() == true);
    REQUIRE(*Required(*boundary, "status").AsString() == "required-not-yet-proven");
}
