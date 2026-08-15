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
#include <utility>
#include <vector>

namespace {

std::string ReadFixture(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" / "whitebox" / name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open M23-C frequency fixture: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

solo_rig::JsonValue::Object RootObject(std::string_view source) {
    const auto parsed = solo_rig::ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        throw std::runtime_error("invalid M23-C frequency golden JSON");
    }
    return *parsed.value->AsObject();
}

const solo_rig::JsonValue& Required(const solo_rig::JsonValue::Object& object, std::string_view key) {
    const auto found = object.find(key);
    if (found == object.end()) throw std::runtime_error("missing M23-C frequency field: " + std::string(key));
    return found->second;
}

double Number(const solo_rig::JsonValue& value, std::string_view field) {
    const auto number = value.AsNumber();
    if (number == nullptr || !std::isfinite(*number)) {
        throw std::runtime_error("invalid M23-C frequency number: " + std::string(field));
    }
    return *number;
}

std::vector<double> Numbers(const solo_rig::JsonValue::Object& object, std::string_view key) {
    const auto* array = Required(object, key).AsArray();
    if (array == nullptr || array->empty()) throw std::runtime_error("invalid M23-C frequency array");
    std::vector<double> values;
    values.reserve(array->size());
    for (const auto& value : *array) values.push_back(Number(value, key));
    return values;
}

std::uint32_t RunUnsigned(const solo_rig::JsonValue::Object& object, std::string_view key) {
    const auto* run = Required(object, "run").AsObject();
    if (run == nullptr) throw std::runtime_error("invalid M23-C frequency run");
    const auto value = Number(Required(*run, key), key);
    if (value < 1.0 || std::floor(value) != value) throw std::runtime_error("invalid M23-C frequency run value");
    return static_cast<std::uint32_t>(value);
}

struct FrequencyMeasurement {
    double magnitude{};
    double phase{};
};

FrequencyMeasurement Measure(const solo_rig::JsonValue::Object& golden, double frequency_hz) {
    const auto prepared = solo_rig::PrepareWhiteboxMnaV2Json(
        ReadFixture("m23c_rc_step_circuit.json"), 192000.0);
    REQUIRE(prepared.error == solo_rig::WhiteboxMnaJsonError::None);
    REQUIRE(prepared.circuit.has_value());
    auto circuit = std::move(*prepared.circuit);
    const auto warmup = RunUnsigned(golden, "warmupSamples");
    const auto measurement_samples = RunUnsigned(golden, "measurementSamples");
    constexpr double sample_rate_hz = 192000.0;
    const auto omega = 2.0 * std::acos(-1.0) * frequency_hz / sample_rate_hz;
    double sine_sum = 0.0;
    double cosine_sum = 0.0;
    for (std::uint32_t sample = 0U; sample < warmup + measurement_samples; ++sample) {
        const auto input = std::sin(omega * static_cast<double>(sample));
        const auto result = circuit.Solve(input);
        REQUIRE(result.Succeeded());
        REQUIRE(std::isfinite(result.output_voltage));
        if (sample >= warmup) {
            sine_sum += result.output_voltage * std::sin(omega * static_cast<double>(sample));
            cosine_sum += result.output_voltage * std::cos(omega * static_cast<double>(sample));
        }
    }
    const auto scale = 2.0 / static_cast<double>(measurement_samples);
    return {.magnitude = scale * std::sqrt(sine_sum * sine_sum + cosine_sum * cosine_sum),
            .phase = std::atan2(cosine_sum, sine_sum)};
}

} // namespace

TEST_CASE("M23-C2b RC discrete frequency golden matches real 192 kHz 4x MNA") {
    const auto golden = RootObject(ReadFixture("m23c_rc_frequency_golden.json"));
    const auto frequencies = Numbers(golden, "frequenciesHz");
    const auto expected_magnitude = Numbers(golden, "expectedMagnitude");
    const auto expected_phase = Numbers(golden, "expectedPhaseRadians");
    REQUIRE(frequencies.size() == expected_magnitude.size());
    REQUIRE(frequencies.size() == expected_phase.size());

    const auto* formula = Required(golden, "formula").AsObject();
    REQUIRE(formula != nullptr);
    const auto capacitance = Number(Required(*formula, "capacitanceFarads"), "capacitanceFarads");
    const auto resistance = Number(Required(*formula, "resistorOhms"), "resistorOhms");
    const auto conductance = capacitance * 192000.0 * 4.0;
    const auto alpha = conductance / (conductance + 1.0 / resistance);
    const auto state_coefficient = std::pow(alpha, 4.0);
    const auto input_coefficient = 1.0 - state_coefficient;

    std::vector<double> formula_magnitudes;
    std::vector<double> formula_phases;
    formula_magnitudes.reserve(frequencies.size());
    formula_phases.reserve(frequencies.size());
    for (const auto frequency : frequencies) {
        const auto omega = 2.0 * std::acos(-1.0) * frequency / 192000.0;
        const auto denominator_real = 1.0 - state_coefficient * std::cos(omega);
        const auto denominator_imaginary = state_coefficient * std::sin(omega);
        formula_magnitudes.push_back(input_coefficient /
            std::sqrt(denominator_real * denominator_real + denominator_imaginary * denominator_imaginary));
        formula_phases.push_back(-std::atan2(denominator_imaginary, denominator_real));
    }
    REQUIRE(solo_rig::CompareEvidenceSamples(formula_magnitudes, expected_magnitude, 1.0e-12).passed);
    REQUIRE(solo_rig::CompareEvidenceSamples(formula_phases, expected_phase, 1.0e-12).passed);

    std::vector<double> measured_magnitudes;
    std::vector<double> measured_phases;
    measured_magnitudes.reserve(frequencies.size());
    measured_phases.reserve(frequencies.size());
    for (const auto frequency : frequencies) {
        const auto measured = Measure(golden, frequency);
        measured_magnitudes.push_back(measured.magnitude);
        measured_phases.push_back(measured.phase);
    }
    const auto tolerance = Number(Required(golden, "absoluteTolerance"), "absoluteTolerance");
    REQUIRE(solo_rig::CompareEvidenceSamples(measured_magnitudes, expected_magnitude, tolerance).passed);
    REQUIRE(solo_rig::CompareEvidenceSamples(measured_phases, expected_phase, tolerance).passed);
}

TEST_CASE("M23-C2b RC frequency evidence package keeps SPICE blocked") {
    const auto parsed = solo_rig::ParseEvidencePackageJson(ReadFixture("m23c_rc_frequency_evidence.json"));
    REQUIRE(parsed.error == std::nullopt);
    REQUIRE(parsed.package.has_value());
    REQUIRE(parsed.package->status == solo_rig::EvidencePackageStatus::Blocked);
    REQUIRE(parsed.package->frequency_response.status == solo_rig::EvidenceSectionStatus::Passed);
    REQUIRE(parsed.package->dynamic_response.status == solo_rig::EvidenceSectionStatus::Passed);
    REQUIRE(parsed.package->golden.status == solo_rig::EvidenceSectionStatus::Passed);
    REQUIRE(parsed.package->spice.status == solo_rig::EvidenceSectionStatus::Blocked);
    REQUIRE(solo_rig::EvaluateEvidencePackage(*parsed.package) == solo_rig::EvidenceEvaluation::Blocked);
}
