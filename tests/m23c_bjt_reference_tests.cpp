#include "content/evidence_chain.hpp"
#include "content/json.hpp"
#include "content/whitebox_mna_json.hpp"
#include "test_framework.hpp"

#include <algorithm>
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
    if (!stream) throw std::runtime_error("cannot open M23-C2d BJT fixture: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

solo_rig::JsonValue::Object RootObject(std::string_view source) {
    const auto parsed = solo_rig::ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        throw std::runtime_error("invalid M23-C2d BJT golden JSON");
    }
    return *parsed.value->AsObject();
}

const solo_rig::JsonValue& Required(const solo_rig::JsonValue::Object& object, std::string_view key) {
    const auto found = object.find(key);
    if (found == object.end()) throw std::runtime_error("missing M23-C2d BJT field: " + std::string(key));
    return found->second;
}

double Number(const solo_rig::JsonValue& value, std::string_view field) {
    const auto number = value.AsNumber();
    if (number == nullptr || !std::isfinite(*number)) {
        throw std::runtime_error("invalid M23-C2d BJT number: " + std::string(field));
    }
    return *number;
}

std::vector<double> Numbers(const solo_rig::JsonValue::Object& object, std::string_view key) {
    const auto* array = Required(object, key).AsArray();
    if (array == nullptr || array->empty()) throw std::runtime_error("invalid M23-C2d BJT array");
    std::vector<double> values;
    values.reserve(array->size());
    for (const auto& value : *array) values.push_back(Number(value, key));
    return values;
}

double FormulaOutput(
    double base, double saturation_current, double forward_beta, double reverse_beta,
    double thermal_voltage, double supply, double collector_ohms, double emitter_ohms) {
    const auto forward_alpha = forward_beta / (forward_beta + 1.0);
    const auto reverse_alpha = reverse_beta / (reverse_beta + 1.0);
    double collector = supply;
    double emitter = 0.0;
    for (int iteration = 0; iteration < 64; ++iteration) {
        const auto forward_exponential = std::exp((base - emitter) / thermal_voltage);
        const auto reverse_exponential = std::exp((base - collector) / thermal_voltage);
        const auto forward_current = saturation_current * (forward_exponential - 1.0);
        const auto reverse_current = saturation_current * (reverse_exponential - 1.0);
        const auto forward_conductance = saturation_current / thermal_voltage * forward_exponential;
        const auto reverse_conductance = saturation_current / thermal_voltage * reverse_exponential;
        const auto collector_current = forward_alpha * forward_current - reverse_current;
        const auto emitter_current = -forward_current + reverse_alpha * reverse_current;
        const auto collector_residual = (collector - supply) / collector_ohms + collector_current;
        const auto emitter_residual = emitter / emitter_ohms + emitter_current;
        const auto j11 = 1.0 / collector_ohms + reverse_conductance;
        const auto j12 = -forward_alpha * forward_conductance;
        const auto j21 = -reverse_alpha * reverse_conductance;
        const auto j22 = 1.0 / emitter_ohms + forward_conductance;
        const auto determinant = j11 * j22 - j12 * j21;
        if (!std::isfinite(determinant) || std::fabs(determinant) < 1.0e-30) {
            throw std::runtime_error("singular M23-C2d BJT formula");
        }
        const auto delta_collector = (-collector_residual * j22 + j12 * emitter_residual) / determinant;
        const auto delta_emitter = (j21 * collector_residual - j11 * emitter_residual) / determinant;
        collector += delta_collector;
        emitter += delta_emitter;
        if (!std::isfinite(collector) || !std::isfinite(emitter)) {
            throw std::runtime_error("non-finite M23-C2d BJT formula");
        }
        if (std::max(std::fabs(delta_collector), std::fabs(delta_emitter)) <= 1.0e-14) break;
    }
    return collector;
}

std::vector<double> SolveFixture(const std::vector<double>& inputs) {
    const auto prepared = solo_rig::PrepareWhiteboxMnaV2Json(
        ReadFixture("m23c_bjt_static_circuit.json"), 192000.0);
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

TEST_CASE("M23-C2d Ebers-Moll BJT static golden matches an independent equation and real MNA") {
    const auto golden = RootObject(ReadFixture("m23c_bjt_static_golden.json"));
    const auto inputs = Numbers(golden, "inputs");
    const auto expected = Numbers(golden, "expectedOutputs");
    REQUIRE(inputs.size() == expected.size());

    const auto* formula = Required(golden, "formula").AsObject();
    REQUIRE(formula != nullptr);
    const auto saturation_current = Number(Required(*formula, "saturationCurrent"), "saturationCurrent");
    const auto forward_beta = Number(Required(*formula, "forwardBeta"), "forwardBeta");
    const auto reverse_beta = Number(Required(*formula, "reverseBeta"), "reverseBeta");
    const auto thermal_voltage = Number(Required(*formula, "thermalVoltage"), "thermalVoltage");
    const auto supply = Number(Required(*formula, "collectorSupply"), "collectorSupply");
    const auto collector_ohms = Number(Required(*formula, "collectorOhms"), "collectorOhms");
    const auto emitter_ohms = Number(Required(*formula, "emitterOhms"), "emitterOhms");

    std::vector<double> formula_outputs;
    formula_outputs.reserve(inputs.size());
    for (const auto input : inputs) {
        formula_outputs.push_back(FormulaOutput(input, saturation_current, forward_beta, reverse_beta,
                                                thermal_voltage, supply, collector_ohms, emitter_ohms));
    }
    REQUIRE(solo_rig::CompareEvidenceSamples(formula_outputs, expected, 1.0e-12).passed);

    const auto outputs = SolveFixture(inputs);
    const auto comparison = solo_rig::CompareEvidenceSamples(
        outputs, expected, Number(Required(golden, "absoluteTolerance"), "absoluteTolerance"));
    REQUIRE(comparison.passed);
}

TEST_CASE("M23-C2d BJT evidence package keeps mathematical progress separate from SPICE") {
    const auto parsed = solo_rig::ParseEvidencePackageJson(ReadFixture("m23c_bjt_static_evidence.json"));
    REQUIRE(parsed.error == std::nullopt);
    REQUIRE(parsed.package.has_value());
    REQUIRE(parsed.package->status == solo_rig::EvidencePackageStatus::Blocked);
    REQUIRE(parsed.package->static_curve.status == solo_rig::EvidenceSectionStatus::Passed);
    REQUIRE(parsed.package->golden.status == solo_rig::EvidenceSectionStatus::Passed);
    REQUIRE(parsed.package->spice.status == solo_rig::EvidenceSectionStatus::Blocked);
    REQUIRE(parsed.package->golden.sample_count == 6U);
    REQUIRE(solo_rig::EvaluateEvidencePackage(*parsed.package) == solo_rig::EvidenceEvaluation::Blocked);
}
