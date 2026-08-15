#include "content/evidence_chain.hpp"
#include "content/json.hpp"
#include "content/whitebox_mna_json.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string ReadFixture(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" / "whitebox" / name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open M23-C fixture: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

solo_rig::JsonValue::Object RootObject(std::string_view source) {
    const auto parsed = solo_rig::ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        throw std::runtime_error("invalid M23-C golden JSON");
    }
    return *parsed.value->AsObject();
}

const solo_rig::JsonValue& Required(const solo_rig::JsonValue::Object& object, std::string_view key) {
    const auto found = object.find(key);
    if (found == object.end()) throw std::runtime_error("missing M23-C golden field: " + std::string(key));
    return found->second;
}

double Number(const solo_rig::JsonValue& value, std::string_view field) {
    const auto number = value.AsNumber();
    if (number == nullptr || !std::isfinite(*number)) {
        throw std::runtime_error("invalid M23-C golden number: " + std::string(field));
    }
    return *number;
}

std::vector<double> Numbers(const solo_rig::JsonValue::Object& object, std::string_view key) {
    const auto* array = Required(object, key).AsArray();
    if (array == nullptr || array->empty()) throw std::runtime_error("invalid M23-C golden array");
    std::vector<double> values;
    values.reserve(array->size());
    for (const auto& value : *array) values.push_back(Number(value, key));
    return values;
}

double RunSampleRate(const solo_rig::JsonValue::Object& object) {
    const auto* run = Required(object, "run").AsObject();
    if (run == nullptr) throw std::runtime_error("invalid M23-C golden run");
    return Number(Required(*run, "sampleRateHz"), "sampleRateHz");
}

std::uint32_t RunOversampling(const solo_rig::JsonValue::Object& object) {
    const auto* run = Required(object, "run").AsObject();
    if (run == nullptr) throw std::runtime_error("invalid M23-C golden run");
    const auto value = Number(Required(*run, "oversampling"), "oversampling");
    if (value < 1.0 || std::floor(value) != value) throw std::runtime_error("invalid M23-C oversampling");
    return static_cast<std::uint32_t>(value);
}

double Tolerance(const solo_rig::JsonValue::Object& object) {
    return Number(Required(object, "absoluteTolerance"), "absoluteTolerance");
}

std::vector<double> SolveFixture(std::string_view circuit_name,
                                 const std::vector<double>& inputs,
                                 double sample_rate_hz) {
    const auto prepared = solo_rig::PrepareWhiteboxMnaV2Json(
        ReadFixture(circuit_name), sample_rate_hz);
    REQUIRE(prepared.error == solo_rig::WhiteboxMnaJsonError::None);
    REQUIRE(prepared.circuit.has_value());
    auto circuit = std::move(*prepared.circuit);
    std::vector<double> outputs;
    outputs.reserve(inputs.size());
    for (const auto input : inputs) {
        const auto result = circuit.Solve(input);
        REQUIRE(result.Succeeded());
        REQUIRE(std::isfinite(result.output_voltage));
        outputs.push_back(result.output_voltage);
    }
    return outputs;
}

} // namespace

TEST_CASE("M23-C self-authored resistor divider golden reaches the real Circuit v2 MNA path") {
    const auto golden = RootObject(ReadFixture("m23c_passive_divider_golden.json"));
    const auto inputs = Numbers(golden, "inputs");
    const auto expected = Numbers(golden, "expectedOutputs");
    REQUIRE(inputs.size() == expected.size());

    const auto outputs = SolveFixture("m23c_passive_divider_circuit.json", inputs, RunSampleRate(golden));
    const auto comparison = solo_rig::CompareEvidenceSamples(outputs, expected, Tolerance(golden));
    REQUIRE(comparison.passed);
    REQUIRE(comparison.max_abs_error <= 1.0e-12);

    const auto* formula = Required(golden, "formula").AsObject();
    REQUIRE(formula != nullptr);
    const auto series = Number(Required(*formula, "seriesOhms"), "seriesOhms");
    const auto load = Number(Required(*formula, "loadOhms"), "loadOhms");
    const auto gain = load / (series + load);
    for (std::size_t index = 0U; index < inputs.size(); ++index) {
        REQUIRE(std::fabs(expected[index] - inputs[index] * gain) <= 1.0e-12);
    }
}

TEST_CASE("M23-C self-authored RC backward-Euler golden matches every oversampled substep") {
    const auto golden = RootObject(ReadFixture("m23c_rc_step_golden.json"));
    const auto inputs = Numbers(golden, "inputs");
    const auto expected = Numbers(golden, "expectedOutputs");
    REQUIRE(inputs.size() == expected.size());

    const auto* formula = Required(golden, "formula").AsObject();
    REQUIRE(formula != nullptr);
    const auto capacitance = Number(Required(*formula, "capacitanceFarads"), "capacitanceFarads");
    const auto resistance = Number(Required(*formula, "resistorOhms"), "resistorOhms");
    const auto capacitor_conductance = capacitance * RunSampleRate(golden) * RunOversampling(golden);
    const auto resistor_conductance = 1.0 / resistance;
    const auto denominator = capacitor_conductance + resistor_conductance;
    double previous = 0.0;
    std::vector<double> formula_outputs;
    formula_outputs.reserve(inputs.size());
    for (const auto input : inputs) {
        for (std::uint32_t substep = 0U; substep < RunOversampling(golden); ++substep) {
            previous = (resistor_conductance * input + capacitor_conductance * previous) / denominator;
        }
        formula_outputs.push_back(previous);
    }
    REQUIRE(solo_rig::CompareEvidenceSamples(formula_outputs, expected, 1.0e-12).passed);

    const auto outputs = SolveFixture("m23c_rc_step_circuit.json", inputs, RunSampleRate(golden));
    const auto comparison = solo_rig::CompareEvidenceSamples(outputs, expected, Tolerance(golden));
    REQUIRE(comparison.passed);
    REQUIRE(comparison.max_abs_error <= Tolerance(golden));
}

TEST_CASE("M23-C evidence package records golden progress without hiding the blocked SPICE gate") {
    const auto parsed = solo_rig::ParseEvidencePackageJson(ReadFixture("m23c_passive_evidence.json"));
    REQUIRE(parsed.error == std::nullopt);
    REQUIRE(parsed.package.has_value());
    REQUIRE(parsed.package->status == solo_rig::EvidencePackageStatus::Blocked);
    REQUIRE(parsed.package->golden.status == solo_rig::EvidenceSectionStatus::Passed);
    REQUIRE(parsed.package->spice.status == solo_rig::EvidenceSectionStatus::Blocked);
    REQUIRE(parsed.package->golden.sample_count == 4U);
    REQUIRE(solo_rig::EvaluateEvidencePackage(*parsed.package) == solo_rig::EvidenceEvaluation::Blocked);
}
