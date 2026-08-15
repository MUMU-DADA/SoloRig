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
    if (!stream) throw std::runtime_error("cannot open M23-C dynamic diode fixture: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

solo_rig::JsonValue::Object RootObject(std::string_view source) {
    const auto parsed = solo_rig::ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        throw std::runtime_error("invalid M23-C dynamic diode golden JSON");
    }
    return *parsed.value->AsObject();
}

const solo_rig::JsonValue& Required(const solo_rig::JsonValue::Object& object, std::string_view key) {
    const auto found = object.find(key);
    if (found == object.end()) throw std::runtime_error("missing M23-C dynamic diode field: " + std::string(key));
    return found->second;
}

double Number(const solo_rig::JsonValue& value, std::string_view field) {
    const auto number = value.AsNumber();
    if (number == nullptr || !std::isfinite(*number)) {
        throw std::runtime_error("invalid M23-C dynamic diode number: " + std::string(field));
    }
    return *number;
}

std::vector<double> Numbers(const solo_rig::JsonValue::Object& object, std::string_view key) {
    const auto* array = Required(object, key).AsArray();
    if (array == nullptr || array->empty()) throw std::runtime_error("invalid M23-C dynamic diode array");
    std::vector<double> values;
    values.reserve(array->size());
    for (const auto& value : *array) values.push_back(Number(value, key));
    return values;
}

double Step(double input, double history, double sample_rate, std::uint32_t oversampling,
            double saturation_current, double thermal_voltage, double resistance,
            double capacitance) {
    const auto conductance_scale = sample_rate * static_cast<double>(oversampling);
    double voltage = history;
    for (std::uint32_t iteration = 0U; iteration < 64U; ++iteration) {
        const auto exponential = std::exp(voltage / thermal_voltage);
        const auto residual = (voltage - input) / resistance +
            saturation_current * (exponential - 1.0) +
            capacitance * conductance_scale * (voltage - history);
        const auto jacobian = 1.0 / resistance + saturation_current / thermal_voltage * exponential +
            capacitance * conductance_scale;
        const auto next = voltage - residual / jacobian;
        if (!std::isfinite(next)) throw std::runtime_error("non-finite M23-C dynamic diode formula");
        if (std::fabs(next - voltage) <= 1.0e-14) return next;
        voltage = next;
    }
    return voltage;
}

std::vector<double> FormulaOutputs(const solo_rig::JsonValue::Object& golden,
                                   const std::vector<double>& inputs) {
    const auto* formula = Required(golden, "formula").AsObject();
    REQUIRE(formula != nullptr);
    const auto saturation_current = Number(Required(*formula, "saturationCurrent"), "saturationCurrent");
    const auto thermal_voltage = Number(Required(*formula, "thermalVoltage"), "thermalVoltage");
    const auto resistance = Number(Required(*formula, "seriesOhms"), "seriesOhms");
    const auto capacitance = Number(Required(*formula, "junctionCapacitanceFarads"), "junctionCapacitanceFarads");
    double history = Number(Required(*formula, "initialVoltage"), "initialVoltage");
    std::vector<double> outputs;
    outputs.reserve(inputs.size());
    for (const auto input : inputs) {
        for (std::uint32_t substep = 0U; substep < 4U; ++substep) {
            history = Step(input, history, 192000.0, 4U, saturation_current, thermal_voltage,
                           resistance, capacitance);
        }
        outputs.push_back(history);
    }
    return outputs;
}

std::vector<double> SolveFixture(const std::vector<double>& inputs) {
    const auto prepared = solo_rig::PrepareWhiteboxMnaV2Json(
        ReadFixture("m23c_diode_dynamic_circuit.json"), 192000.0);
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

TEST_CASE("M23-C2c dynamic Shockley diode golden matches every 192 kHz 4x MNA substep") {
    const auto golden = RootObject(ReadFixture("m23c_diode_dynamic_golden.json"));
    const auto inputs = Numbers(golden, "inputs");
    const auto expected = Numbers(golden, "expectedOutputs");
    REQUIRE(inputs.size() == expected.size());

    const auto formula_outputs = FormulaOutputs(golden, inputs);
    REQUIRE(solo_rig::CompareEvidenceSamples(formula_outputs, expected, 1.0e-12).passed);
    const auto outputs = SolveFixture(inputs);
    const auto comparison = solo_rig::CompareEvidenceSamples(
        outputs, expected, Number(Required(golden, "absoluteTolerance"), "absoluteTolerance"));
    REQUIRE(comparison.passed);
}

TEST_CASE("M23-C2c dynamic diode evidence package keeps SPICE blocked") {
    const auto parsed = solo_rig::ParseEvidencePackageJson(ReadFixture("m23c_diode_dynamic_evidence.json"));
    REQUIRE(parsed.error == std::nullopt);
    REQUIRE(parsed.package.has_value());
    REQUIRE(parsed.package->status == solo_rig::EvidencePackageStatus::Blocked);
    REQUIRE(parsed.package->dynamic_response.status == solo_rig::EvidenceSectionStatus::Passed);
    REQUIRE(parsed.package->golden.status == solo_rig::EvidenceSectionStatus::Passed);
    REQUIRE(parsed.package->spice.status == solo_rig::EvidenceSectionStatus::Blocked);
    REQUIRE(parsed.package->dynamic_response.sample_count == 5U);
    REQUIRE(solo_rig::EvaluateEvidencePackage(*parsed.package) == solo_rig::EvidenceEvaluation::Blocked);
}
