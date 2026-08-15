#include "content/evidence_chain.hpp"
#include "content/json.hpp"
#include "content/whitebox_mna_json.hpp"
#include "test_framework.hpp"

#include <cmath>
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
    if (!stream) throw std::runtime_error("cannot open M23-C2f MOSFET fixture: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

solo_rig::JsonValue::Object RootObject(std::string_view source) {
    const auto parsed = solo_rig::ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        throw std::runtime_error("invalid M23-C2f MOSFET golden JSON");
    }
    return *parsed.value->AsObject();
}

const solo_rig::JsonValue& Required(const solo_rig::JsonValue::Object& object, std::string_view key) {
    const auto found = object.find(key);
    if (found == object.end()) throw std::runtime_error("missing M23-C2f MOSFET field: " + std::string(key));
    return found->second;
}

double Number(const solo_rig::JsonValue& value, std::string_view field) {
    const auto number = value.AsNumber();
    if (number == nullptr || !std::isfinite(*number)) {
        throw std::runtime_error("invalid M23-C2f MOSFET number: " + std::string(field));
    }
    return *number;
}

std::vector<double> Numbers(const solo_rig::JsonValue::Object& object, std::string_view key) {
    const auto* array = Required(object, key).AsArray();
    if (array == nullptr || array->empty()) throw std::runtime_error("invalid M23-C2f MOSFET array");
    std::vector<double> values;
    values.reserve(array->size());
    for (const auto& value : *array) values.push_back(Number(value, key));
    return values;
}

struct ChannelEvaluation {
    double current{};
    double derivative_overdrive{};
    double derivative_voltage{};
};

ChannelEvaluation EvaluateChannel(
    double overdrive, double voltage, double parameter, double max_current) {
    if (overdrive <= 0.0) return {};
    const auto limit = max_current / parameter;
    if (voltage < overdrive) {
        const auto factor = 2.0 * overdrive - voltage;
        if (voltage > limit / factor) return {.current = max_current};
        const auto current = parameter * (2.0 * overdrive * voltage - voltage * voltage);
        return {.current = current,
                .derivative_overdrive = 2.0 * parameter * voltage,
                .derivative_voltage = 2.0 * parameter * (overdrive - voltage)};
    }
    if (overdrive > std::sqrt(limit)) return {.current = max_current};
    return {.current = parameter * overdrive * overdrive,
            .derivative_overdrive = 2.0 * parameter * overdrive,
            .derivative_voltage = 0.0};
}

double FormulaOutput(
    double gate, double threshold_voltage, double parameter, double max_current,
    double supply, double drain_ohms) {
    double drain = supply;
    for (int iteration = 0; iteration < 64; ++iteration) {
        const auto channel = EvaluateChannel(gate - threshold_voltage, drain, parameter, max_current);
        const auto residual = (drain - supply) / drain_ohms + channel.current;
        const auto derivative = 1.0 / drain_ohms + channel.derivative_voltage;
        const auto delta = -residual / derivative;
        drain += delta;
        if (!std::isfinite(drain)) throw std::runtime_error("non-finite M23-C2f MOSFET formula");
        if (std::fabs(delta) <= 1.0e-14) break;
    }
    return drain;
}

std::vector<double> SolveFixture(const std::vector<double>& inputs) {
    const auto prepared = solo_rig::PrepareWhiteboxMnaV2Json(
        ReadFixture("m23c_mosfet_static_circuit.json"), 192000.0);
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

TEST_CASE("M23-C2f square-law MOSFET static golden matches an independent equation and real MNA") {
    const auto golden = RootObject(ReadFixture("m23c_mosfet_static_golden.json"));
    const auto inputs = Numbers(golden, "inputs");
    const auto expected = Numbers(golden, "expectedOutputs");
    REQUIRE(inputs.size() == expected.size());

    const auto* formula = Required(golden, "formula").AsObject();
    REQUIRE(formula != nullptr);
    const auto threshold_voltage = Number(Required(*formula, "thresholdVoltage"), "thresholdVoltage");
    const auto parameter = Number(Required(*formula, "transconductanceParameter"), "transconductanceParameter");
    const auto max_current = Number(Required(*formula, "maxCurrent"), "maxCurrent");
    const auto supply = Number(Required(*formula, "drainSupply"), "drainSupply");
    const auto drain_ohms = Number(Required(*formula, "drainOhms"), "drainOhms");

    std::vector<double> formula_outputs;
    formula_outputs.reserve(inputs.size());
    for (const auto input : inputs) {
        formula_outputs.push_back(FormulaOutput(input, threshold_voltage, parameter, max_current,
                                                supply, drain_ohms));
    }
    REQUIRE(solo_rig::CompareEvidenceSamples(formula_outputs, expected, 1.0e-12).passed);

    const auto outputs = SolveFixture(inputs);
    const auto comparison = solo_rig::CompareEvidenceSamples(
        outputs, expected, Number(Required(golden, "absoluteTolerance"), "absoluteTolerance"));
    REQUIRE(comparison.passed);
}

TEST_CASE("M23-C2f MOSFET evidence package keeps mathematical progress separate from SPICE") {
    const auto parsed = solo_rig::ParseEvidencePackageJson(ReadFixture("m23c_mosfet_static_evidence.json"));
    REQUIRE(parsed.error == std::nullopt);
    REQUIRE(parsed.package.has_value());
    REQUIRE(parsed.package->status == solo_rig::EvidencePackageStatus::Blocked);
    REQUIRE(parsed.package->static_curve.status == solo_rig::EvidenceSectionStatus::Passed);
    REQUIRE(parsed.package->golden.status == solo_rig::EvidenceSectionStatus::Passed);
    REQUIRE(parsed.package->spice.status == solo_rig::EvidenceSectionStatus::Blocked);
    REQUIRE(parsed.package->golden.sample_count == 8U);
    REQUIRE(solo_rig::EvaluateEvidencePackage(*parsed.package) == solo_rig::EvidenceEvaluation::Blocked);
}
