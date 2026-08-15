#include "content/evidence_chain.hpp"
#include "content/json.hpp"
#include "content/whitebox_mna_json.hpp"
#include "test_framework.hpp"

#include <algorithm>
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
    if (!stream) throw std::runtime_error("cannot open M23-C2g triode fixture: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

solo_rig::JsonValue::Object RootObject(std::string_view source) {
    const auto parsed = solo_rig::ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        throw std::runtime_error("invalid M23-C2g triode golden JSON");
    }
    return *parsed.value->AsObject();
}

const solo_rig::JsonValue& Required(const solo_rig::JsonValue::Object& object, std::string_view key) {
    const auto found = object.find(key);
    if (found == object.end()) throw std::runtime_error("missing M23-C2g triode field: " + std::string(key));
    return found->second;
}

double Number(const solo_rig::JsonValue& value, std::string_view field) {
    const auto number = value.AsNumber();
    if (number == nullptr || !std::isfinite(*number)) {
        throw std::runtime_error("invalid M23-C2g triode number: " + std::string(field));
    }
    return *number;
}

std::vector<double> Numbers(const solo_rig::JsonValue::Object& object, std::string_view key) {
    const auto* array = Required(object, key).AsArray();
    if (array == nullptr || array->empty()) throw std::runtime_error("invalid M23-C2g triode array");
    std::vector<double> values;
    values.reserve(array->size());
    for (const auto& value : *array) values.push_back(Number(value, key));
    return values;
}

double FormulaOutput(
    double grid, double amplification_factor, double perveance, double max_current,
    double supply, double plate_ohms) {
    double plate = supply;
    for (int iteration = 0; iteration < 64; ++iteration) {
        const auto control = grid + plate / amplification_factor;
        double current = 0.0;
        double derivative_plate = 0.0;
        if (plate > 0.0 && control > 0.0) {
            const auto control_limit = std::pow(max_current / perveance, 2.0 / 3.0);
            if (control >= control_limit) {
                current = max_current;
            } else {
                const auto root = std::sqrt(control);
                current = perveance * control * root;
                derivative_plate = 1.5 * perveance * root / amplification_factor;
            }
        }
        const auto residual = (plate - supply) / plate_ohms + current;
        const auto derivative = 1.0 / plate_ohms + derivative_plate;
        const auto delta = -residual / derivative;
        plate += delta;
        if (!std::isfinite(plate)) throw std::runtime_error("non-finite M23-C2g triode formula");
        if (std::fabs(delta) <= 1.0e-14) break;
    }
    return plate;
}

std::vector<double> SolveFixture(const std::vector<double>& inputs) {
    const auto prepared = solo_rig::PrepareWhiteboxMnaV2Json(
        ReadFixture("m23c_triode_static_circuit.json"), 192000.0);
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

double DynamicPlateOutput(
    double grid, double amplification_factor, double perveance, double max_current,
    double supply, double plate_ohms) {
    double plate = supply;
    for (int iteration = 0; iteration < 64; ++iteration) {
        const auto control = grid + plate / amplification_factor;
        double current = 0.0;
        double derivative_plate = 0.0;
        if (plate > 0.0 && control > 0.0) {
            const auto control_limit = std::pow(max_current / perveance, 2.0 / 3.0);
            if (control >= control_limit) {
                current = max_current;
            } else {
                const auto root = std::sqrt(control);
                current = perveance * control * root;
                derivative_plate = 1.5 * perveance * root / amplification_factor;
            }
        }
        const auto residual = (plate - supply) / plate_ohms + current;
        const auto derivative = 1.0 / plate_ohms + derivative_plate;
        const auto delta = -residual / derivative;
        plate += delta;
        if (!std::isfinite(plate)) throw std::runtime_error("non-finite M23-C2h triode formula");
        if (std::fabs(delta) <= 1.0e-14) break;
    }
    return plate;
}

std::vector<double> DynamicFormulaOutputs(
    const std::vector<double>& inputs, double input_ohms, double capacitance,
    double initial_grid, double sample_rate, std::uint32_t oversampling,
    double amplification_factor, double perveance, double max_current,
    double supply, double plate_ohms) {
    const auto input_conductance = 1.0 / input_ohms;
    const auto capacitor_conductance = capacitance * sample_rate * static_cast<double>(oversampling);
    double grid_history = initial_grid;
    std::vector<double> outputs;
    outputs.reserve(inputs.size());
    for (const auto input : inputs) {
        double plate = supply;
        for (std::uint32_t substep = 0; substep < oversampling; ++substep) {
            const auto grid = (input_conductance * input + capacitor_conductance * grid_history) /
                (input_conductance + capacitor_conductance);
            plate = DynamicPlateOutput(grid, amplification_factor, perveance, max_current, supply, plate_ohms);
            grid_history = grid;
        }
        outputs.push_back(plate);
    }
    return outputs;
}

std::vector<double> SolveDynamicFixture(const std::vector<double>& inputs) {
    const auto prepared = solo_rig::PrepareWhiteboxMnaV2Json(
        ReadFixture("m23c_triode_dynamic_circuit.json"), 192000.0);
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

TEST_CASE("M23-C2g space-charge triode static golden matches an independent equation and real MNA") {
    const auto golden = RootObject(ReadFixture("m23c_triode_static_golden.json"));
    const auto inputs = Numbers(golden, "inputs");
    const auto expected = Numbers(golden, "expectedOutputs");
    REQUIRE(inputs.size() == expected.size());

    const auto* formula = Required(golden, "formula").AsObject();
    REQUIRE(formula != nullptr);
    const auto amplification_factor = Number(Required(*formula, "amplificationFactor"), "amplificationFactor");
    const auto perveance = Number(Required(*formula, "perveance"), "perveance");
    const auto max_current = Number(Required(*formula, "maxCurrent"), "maxCurrent");
    const auto supply = Number(Required(*formula, "plateSupply"), "plateSupply");
    const auto plate_ohms = Number(Required(*formula, "plateOhms"), "plateOhms");

    std::vector<double> formula_outputs;
    formula_outputs.reserve(inputs.size());
    for (const auto input : inputs) {
        formula_outputs.push_back(FormulaOutput(input, amplification_factor, perveance, max_current,
                                                supply, plate_ohms));
    }
    REQUIRE(solo_rig::CompareEvidenceSamples(formula_outputs, expected, 1.0e-12).passed);

    const auto outputs = SolveFixture(inputs);
    const auto comparison = solo_rig::CompareEvidenceSamples(
        outputs, expected, Number(Required(golden, "absoluteTolerance"), "absoluteTolerance"));
    REQUIRE(comparison.passed);
}

TEST_CASE("M23-C2g triode evidence package keeps mathematical progress separate from SPICE") {
    const auto parsed = solo_rig::ParseEvidencePackageJson(ReadFixture("m23c_triode_static_evidence.json"));
    REQUIRE(parsed.error == std::nullopt);
    REQUIRE(parsed.package.has_value());
    REQUIRE(parsed.package->status == solo_rig::EvidencePackageStatus::Blocked);
    REQUIRE(parsed.package->static_curve.status == solo_rig::EvidenceSectionStatus::Passed);
    REQUIRE(parsed.package->golden.status == solo_rig::EvidenceSectionStatus::Passed);
    REQUIRE(parsed.package->spice.status == solo_rig::EvidenceSectionStatus::Blocked);
    REQUIRE(parsed.package->golden.sample_count == 7U);
    REQUIRE(solo_rig::EvaluateEvidencePackage(*parsed.package) == solo_rig::EvidenceEvaluation::Blocked);
}

TEST_CASE("M23-C2h dynamic triode golden matches independent backward-Euler and real MNA") {
    const auto golden = RootObject(ReadFixture("m23c_triode_dynamic_golden.json"));
    const auto inputs = Numbers(golden, "inputs");
    const auto expected = Numbers(golden, "expectedOutputs");
    REQUIRE(inputs.size() == expected.size());

    const auto* formula = Required(golden, "formula").AsObject();
    REQUIRE(formula != nullptr);
    const auto amplification_factor = Number(Required(*formula, "amplificationFactor"), "amplificationFactor");
    const auto perveance = Number(Required(*formula, "perveance"), "perveance");
    const auto max_current = Number(Required(*formula, "maxCurrent"), "maxCurrent");
    const auto input_ohms = Number(Required(*formula, "inputOhms"), "inputOhms");
    const auto capacitance = Number(Required(*formula, "gridCapacitanceFarads"), "gridCapacitanceFarads");
    const auto initial_grid = Number(Required(*formula, "initialGridVoltage"), "initialGridVoltage");
    const auto supply = Number(Required(*formula, "plateSupply"), "plateSupply");
    const auto plate_ohms = Number(Required(*formula, "plateOhms"), "plateOhms");
    const auto* run = Required(golden, "run").AsObject();
    REQUIRE(run != nullptr);
    const auto sample_rate = Number(Required(*run, "sampleRateHz"), "sampleRateHz");
    const auto oversampling = static_cast<std::uint32_t>(
        Number(Required(*run, "oversampling"), "oversampling"));

    const auto formula_outputs = DynamicFormulaOutputs(
        inputs, input_ohms, capacitance, initial_grid, sample_rate, oversampling,
        amplification_factor, perveance, max_current, supply, plate_ohms);
    REQUIRE(solo_rig::CompareEvidenceSamples(formula_outputs, expected, 1.0e-12).passed);

    const auto outputs = SolveDynamicFixture(inputs);
    const auto comparison = solo_rig::CompareEvidenceSamples(
        outputs, expected, Number(Required(golden, "absoluteTolerance"), "absoluteTolerance"));
    REQUIRE(comparison.passed);
}

TEST_CASE("M23-C2h dynamic triode evidence package keeps SPICE blocked") {
    const auto parsed = solo_rig::ParseEvidencePackageJson(ReadFixture("m23c_triode_dynamic_evidence.json"));
    REQUIRE(parsed.error == std::nullopt);
    REQUIRE(parsed.package.has_value());
    REQUIRE(parsed.package->status == solo_rig::EvidencePackageStatus::Blocked);
    REQUIRE(parsed.package->dynamic_response.status == solo_rig::EvidenceSectionStatus::Passed);
    REQUIRE(parsed.package->golden.status == solo_rig::EvidenceSectionStatus::Passed);
    REQUIRE(parsed.package->spice.status == solo_rig::EvidenceSectionStatus::Blocked);
    REQUIRE(parsed.package->golden.sample_count == 7U);
    REQUIRE(solo_rig::EvaluateEvidencePackage(*parsed.package) == solo_rig::EvidenceEvaluation::Blocked);
}
