#include "content/evidence_chain.hpp"
#include "content/json.hpp"
#include "content/whitebox_mna_json.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <array>
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
    if (!stream) throw std::runtime_error("cannot open M23-C3a pentode fixture: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

solo_rig::JsonValue::Object RootObject(std::string_view source) {
    const auto parsed = solo_rig::ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        throw std::runtime_error("invalid M23-C3a pentode golden JSON");
    }
    return *parsed.value->AsObject();
}

const solo_rig::JsonValue& Required(const solo_rig::JsonValue::Object& object, std::string_view key) {
    const auto found = object.find(key);
    if (found == object.end()) throw std::runtime_error("missing M23-C3a pentode field: " + std::string(key));
    return found->second;
}

double Number(const solo_rig::JsonValue& value, std::string_view field) {
    const auto number = value.AsNumber();
    if (number == nullptr || !std::isfinite(*number)) {
        throw std::runtime_error("invalid M23-C3a pentode number: " + std::string(field));
    }
    return *number;
}

std::vector<double> Numbers(const solo_rig::JsonValue::Object& object, std::string_view key) {
    const auto* array = Required(object, key).AsArray();
    if (array == nullptr || array->empty()) throw std::runtime_error("invalid M23-C3a pentode array");
    std::vector<double> values;
    values.reserve(array->size());
    for (const auto& value : *array) values.push_back(Number(value, key));
    return values;
}

struct PentodeCurrents {
    double plate{};
    double screen{};
    double d_plate_d_plate{};
    double d_plate_d_screen{};
    double d_screen_d_plate{};
    double d_screen_d_screen{};
};

PentodeCurrents EvaluatePentode(
    double plate, double screen, double grid, double screen_amplification_factor,
    double perveance, double knee_voltage, double screen_current_fraction, double max_current) {
    const double scale = std::max({1.0, std::fabs(plate), std::fabs(screen), std::fabs(grid)});
    const double normalized_plate = plate / scale;
    const double normalized_screen = screen / scale;
    const double normalized_control = grid / scale +
        normalized_screen / screen_amplification_factor;
    PentodeCurrents result{};
    if (normalized_plate <= 0.0 || normalized_screen <= 0.0 || normalized_control <= 0.0) {
        return result;
    }

    const double control_limit = std::pow(max_current / perveance, 2.0 / 3.0);
    double space_current = max_current;
    double derivative_control = 0.0;
    if (normalized_control < control_limit / scale) {
        const double control = normalized_control * scale;
        const double root = std::sqrt(control);
        space_current = perveance * control * root;
        derivative_control = 1.5 * perveance * root;
    }

    double plate_share = 1.0 - std::exp(-60.0);
    double derivative_share = 0.0;
    if (normalized_plate < 60.0 * knee_voltage / scale) {
        const double exponent = std::exp(-(normalized_plate * scale) / knee_voltage);
        plate_share = 1.0 - exponent;
        derivative_share = exponent / knee_voltage;
    }

    const double plate_fraction = 1.0 - screen_current_fraction;
    result.plate = plate_fraction * space_current * plate_share;
    result.screen = screen_current_fraction * space_current;
    const double d_space_d_screen = derivative_control / screen_amplification_factor;
    result.d_plate_d_plate = plate_fraction * space_current * derivative_share;
    result.d_plate_d_screen = plate_fraction * d_space_d_screen * plate_share;
    result.d_screen_d_plate = 0.0;
    result.d_screen_d_screen = screen_current_fraction * d_space_d_screen;
    return result;
}

std::array<double, 2> SolvePentode(
    double grid, double supply, double plate_ohms, double screen_ohms,
    double screen_amplification_factor, double perveance, double knee_voltage,
    double screen_current_fraction, double max_current,
    double initial_plate, double initial_screen) {
    double plate = initial_plate;
    double screen = initial_screen;
    for (int iteration = 0; iteration < 128; ++iteration) {
        const auto currents = EvaluatePentode(
            plate, screen, grid, screen_amplification_factor, perveance,
            knee_voltage, screen_current_fraction, max_current);
        const double residual_plate = (plate - supply) / plate_ohms + currents.plate;
        const double residual_screen = (screen - supply) / screen_ohms + currents.screen;
        const double a = 1.0 / plate_ohms + currents.d_plate_d_plate;
        const double b = currents.d_plate_d_screen;
        const double c = currents.d_screen_d_plate;
        const double d = 1.0 / screen_ohms + currents.d_screen_d_screen;
        const double determinant = a * d - b * c;
        if (!std::isfinite(determinant) || determinant == 0.0) {
            throw std::runtime_error("singular M23-C3a pentode reference");
        }
        const double delta_plate = (-residual_plate * d + b * residual_screen) / determinant;
        const double delta_screen = (c * residual_plate - a * residual_screen) / determinant;
        plate += delta_plate;
        screen += delta_screen;
        if (!std::isfinite(plate) || !std::isfinite(screen)) {
            throw std::runtime_error("non-finite M23-C3a pentode formula");
        }
        if (std::max(std::fabs(delta_plate), std::fabs(delta_screen)) <= 1.0e-14) break;
    }
    return {plate, screen};
}

std::vector<double> DynamicFormulaOutputs(
    const std::vector<double>& inputs, double input_ohms, double capacitance,
    double initial_grid, double sample_rate, std::uint32_t oversampling,
    double screen_amplification_factor, double perveance, double knee_voltage,
    double screen_current_fraction, double max_current, double supply,
    double plate_ohms, double screen_ohms) {
    const double input_conductance = 1.0 / input_ohms;
    const double capacitor_conductance = capacitance * sample_rate * static_cast<double>(oversampling);
    double grid_history = initial_grid;
    double plate_history = supply;
    double screen_history = supply;
    std::vector<double> outputs;
    outputs.reserve(inputs.size());
    for (const auto input : inputs) {
        for (std::uint32_t substep = 0; substep < oversampling; ++substep) {
            const double grid = (input_conductance * input + capacitor_conductance * grid_history) /
                (input_conductance + capacitor_conductance);
            const auto solution = SolvePentode(
                grid, supply, plate_ohms, screen_ohms, screen_amplification_factor,
                perveance, knee_voltage, screen_current_fraction, max_current,
                plate_history, screen_history);
            plate_history = solution[0];
            screen_history = solution[1];
            grid_history = grid;
        }
        outputs.push_back(plate_history);
    }
    return outputs;
}

std::vector<double> SolveDynamicFixture(const std::vector<double>& inputs) {
    const auto prepared = solo_rig::PrepareWhiteboxMnaV2Json(
        ReadFixture("m23c_pentode_dynamic_circuit.json"), 192000.0);
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

TEST_CASE("M23-C3a dynamic space-charge pentode golden matches an independent 2x2 Newton solve and real MNA") {
    const auto golden = RootObject(ReadFixture("m23c_pentode_dynamic_golden.json"));
    const auto inputs = Numbers(golden, "inputs");
    const auto expected = Numbers(golden, "expectedOutputs");
    REQUIRE(inputs.size() == expected.size());

    const auto* formula = Required(golden, "formula").AsObject();
    REQUIRE(formula != nullptr);
    const auto screen_amplification_factor = Number(Required(*formula, "screenAmplificationFactor"), "screenAmplificationFactor");
    const auto perveance = Number(Required(*formula, "perveance"), "perveance");
    const auto knee_voltage = Number(Required(*formula, "kneeVoltage"), "kneeVoltage");
    const auto screen_current_fraction = Number(Required(*formula, "screenCurrentFraction"), "screenCurrentFraction");
    const auto max_current = Number(Required(*formula, "maxCurrent"), "maxCurrent");
    const auto input_ohms = Number(Required(*formula, "inputOhms"), "inputOhms");
    const auto capacitance = Number(Required(*formula, "gridCapacitanceFarads"), "gridCapacitanceFarads");
    const auto initial_grid = Number(Required(*formula, "initialGridVoltage"), "initialGridVoltage");
    const auto supply = Number(Required(*formula, "plateSupply"), "plateSupply");
    const auto plate_ohms = Number(Required(*formula, "plateOhms"), "plateOhms");
    const auto screen_ohms = Number(Required(*formula, "screenOhms"), "screenOhms");
    const auto* run = Required(golden, "run").AsObject();
    REQUIRE(run != nullptr);
    const auto sample_rate = Number(Required(*run, "sampleRateHz"), "sampleRateHz");
    const auto oversampling = static_cast<std::uint32_t>(Number(Required(*run, "oversampling"), "oversampling"));

    const auto formula_outputs = DynamicFormulaOutputs(
        inputs, input_ohms, capacitance, initial_grid, sample_rate, oversampling,
        screen_amplification_factor, perveance, knee_voltage, screen_current_fraction,
        max_current, supply, plate_ohms, screen_ohms);
    REQUIRE(solo_rig::CompareEvidenceSamples(formula_outputs, expected, 1.0e-12).passed);

    const auto outputs = SolveDynamicFixture(inputs);
    const auto comparison = solo_rig::CompareEvidenceSamples(
        outputs, expected, Number(Required(golden, "absoluteTolerance"), "absoluteTolerance"));
    REQUIRE(comparison.passed);
}

TEST_CASE("M23-C3a pentode evidence package keeps self-authored progress separate from SPICE") {
    const auto parsed = solo_rig::ParseEvidencePackageJson(ReadFixture("m23c_pentode_dynamic_evidence.json"));
    REQUIRE(parsed.error == std::nullopt);
    REQUIRE(parsed.package.has_value());
    REQUIRE(parsed.package->status == solo_rig::EvidencePackageStatus::Blocked);
    REQUIRE(parsed.package->dynamic_response.status == solo_rig::EvidenceSectionStatus::Passed);
    REQUIRE(parsed.package->golden.status == solo_rig::EvidenceSectionStatus::Passed);
    REQUIRE(parsed.package->spice.status == solo_rig::EvidenceSectionStatus::Blocked);
    REQUIRE(parsed.package->golden.sample_count == 7U);
    REQUIRE(solo_rig::EvaluateEvidencePackage(*parsed.package) == solo_rig::EvidenceEvaluation::Blocked);
}
