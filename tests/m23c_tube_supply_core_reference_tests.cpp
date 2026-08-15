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

using Vector8 = std::array<double, 8U>;
using Matrix8 = std::array<std::array<double, 8U>, 8U>;

std::string ReadFixture(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" / "whitebox" / name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open M23-C3c supply/core fixture: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

solo_rig::JsonValue::Object RootObject(std::string_view source) {
    const auto parsed = solo_rig::ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        throw std::runtime_error("invalid M23-C3c supply/core golden JSON");
    }
    return *parsed.value->AsObject();
}

const solo_rig::JsonValue& Required(const solo_rig::JsonValue::Object& object, std::string_view key) {
    const auto found = object.find(key);
    if (found == object.end()) throw std::runtime_error("missing M23-C3c supply/core field: " + std::string(key));
    return found->second;
}

double Number(const solo_rig::JsonValue& value, std::string_view field) {
    const auto number = value.AsNumber();
    if (number == nullptr || !std::isfinite(*number)) {
        throw std::runtime_error("invalid M23-C3c supply/core number: " + std::string(field));
    }
    return *number;
}

std::vector<double> Numbers(const solo_rig::JsonValue::Object& object, std::string_view key) {
    const auto* array = Required(object, key).AsArray();
    if (array == nullptr || array->empty()) throw std::runtime_error("invalid M23-C3c supply/core array");
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
    double d_plate_d_grid{};
    double d_screen_d_plate{};
    double d_screen_d_screen{};
    double d_screen_d_grid{};
};

PentodeCurrents EvaluatePentode(
    double plate, double screen, double grid, double screen_amplification_factor,
    double perveance, double knee_voltage, double screen_current_fraction, double max_current) {
    const double scale = std::max({1.0, std::fabs(plate), std::fabs(screen), std::fabs(grid)});
    const double normalized_plate = plate / scale;
    const double normalized_screen = screen / scale;
    const double normalized_control = grid / scale + normalized_screen / screen_amplification_factor;
    PentodeCurrents result{};
    if (normalized_plate <= 0.0 || normalized_screen <= 0.0 || normalized_control <= 0.0) return result;
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
    result.d_plate_d_plate = plate_fraction * space_current * derivative_share;
    result.d_plate_d_screen = plate_fraction * derivative_control / screen_amplification_factor * plate_share;
    result.d_plate_d_grid = plate_fraction * derivative_control * plate_share;
    result.d_screen_d_plate = 0.0;
    result.d_screen_d_screen = screen_current_fraction * derivative_control / screen_amplification_factor;
    result.d_screen_d_grid = screen_current_fraction * derivative_control;
    return result;
}

struct SupplyState {
    double sag{};
    double ripple_phase{};
};

struct SupplyEvaluation {
    double target_voltage{};
    double target_derivative_current{};
    SupplyState next_state{};
};

SupplyEvaluation EvaluateSupply(double branch_current, const SupplyState& previous_state,
                                double timestep_seconds, double voltage,
                                double sag_resistance, double recovery_time_seconds,
                                double maximum_sag, double maximum_load_current) {
    double load_current{};
    double load_derivative{};
    if (branch_current < 0.0) {
        if (branch_current <= -maximum_load_current) load_current = maximum_load_current;
        else {
            load_current = -branch_current;
            load_derivative = -1.0;
        }
    }
    const double ratio = timestep_seconds / recovery_time_seconds;
    const double alpha = ratio / (1.0 + ratio);
    const double raw_sag = previous_state.sag +
        alpha * (sag_resistance * load_current - previous_state.sag);
    double sag_derivative = alpha * sag_resistance * load_derivative;
    double sag = raw_sag;
    if (raw_sag <= 0.0) {
        sag = 0.0;
        sag_derivative = 0.0;
    } else if (raw_sag >= maximum_sag) {
        sag = maximum_sag;
        sag_derivative = 0.0;
    }
    return {
        .target_voltage = voltage - sag,
        .target_derivative_current = -sag_derivative,
        .next_state = {.sag = sag, .ripple_phase = previous_state.ripple_phase}}
        ;
}

struct CoreState {
    double flux{};
    double hysteresis{};
};

struct CoreEvaluation {
    double flux{};
    double differential_flux{};
};

CoreEvaluation EvaluateCore(double magnetizing_current, double hysteresis,
                            double magnetizing_inductance, double saturation_flux,
                            double hysteresis_width, double max_flux, double max_current) {
    const double current = std::clamp(magnetizing_current, -max_current, max_current);
    const double bias = std::clamp(hysteresis, -hysteresis_width, hysteresis_width);
    const double normalized = magnetizing_inductance * current / saturation_flux;
    const double shape = std::tanh(normalized);
    const double derivative = magnetizing_inductance * (1.0 - shape * shape);
    const double raw_flux = saturation_flux * shape + bias;
    if (std::fabs(raw_flux) >= max_flux) {
        return {.flux = raw_flux < 0.0 ? -max_flux : max_flux, .differential_flux = 0.0};
    }
    return {.flux = raw_flux, .differential_flux = derivative};
}

bool SolveLinear(Matrix8 matrix, Vector8 rhs, Vector8& result) {
    for (std::size_t pivot = 0; pivot < 8U; ++pivot) {
        std::size_t best = pivot;
        for (std::size_t row = pivot + 1U; row < 8U; ++row) {
            if (std::fabs(matrix[row][pivot]) > std::fabs(matrix[best][pivot])) best = row;
        }
        if (!std::isfinite(matrix[best][pivot]) || std::fabs(matrix[best][pivot]) <= 1.0e-18) return false;
        if (best != pivot) {
            std::swap(matrix[best], matrix[pivot]);
            std::swap(rhs[best], rhs[pivot]);
        }
        for (std::size_t row = pivot + 1U; row < 8U; ++row) {
            const double factor = matrix[row][pivot] / matrix[pivot][pivot];
            for (std::size_t column = pivot; column < 8U; ++column) {
                matrix[row][column] -= factor * matrix[pivot][column];
            }
            rhs[row] -= factor * rhs[pivot];
        }
    }
    for (std::size_t reverse = 0; reverse < 8U; ++reverse) {
        const std::size_t row = 7U - reverse;
        double value = rhs[row];
        for (std::size_t column = row + 1U; column < 8U; ++column) value -= matrix[row][column] * result[column];
        result[row] = value / matrix[row][row];
    }
    return std::all_of(result.begin(), result.end(), [](double value) { return std::isfinite(value); });
}

struct CompositeState {
    double grid_history{-1.0};
    double primary_current{};
    double secondary_current{};
    SupplyState supply{};
    CoreState core{};
};

Vector8 SolveCompositeStep(
    double input, double timestep_seconds, CompositeState& state,
    double input_ohms, double capacitance, double supply_voltage, double source_resistance,
    double sag_resistance, double recovery_time_seconds, double maximum_sag,
    double maximum_load_current, double screen_ohms, double secondary_load_ohms,
    double screen_amplification_factor, double perveance, double knee_voltage,
    double screen_current_fraction, double max_current, double turns_ratio,
    double magnetizing_inductance, double primary_leakage_inductance,
    double secondary_leakage_inductance, double primary_resistance,
    double secondary_resistance, double saturation_flux, double hysteresis_width,
    double hysteresis_rate, double max_flux, double transformer_max_current) {
    const double input_conductance = 1.0 / input_ohms;
    const double capacitor_conductance = capacitance / timestep_seconds;
    const double primary_dynamic = primary_leakage_inductance / timestep_seconds;
    const double secondary_dynamic = secondary_leakage_inductance / timestep_seconds;
    const double core_previous_flux = state.core.flux;
    Vector8 solution{state.grid_history, supply_voltage, supply_voltage, supply_voltage, 0.0,
                     state.primary_current, state.secondary_current, 0.0};
    for (int iteration = 0; iteration < 128; ++iteration) {
        const double grid = solution[0];
        const double plate = solution[1];
        const double screen = solution[2];
        const double rail = solution[3];
        const double secondary = solution[4];
        const double primary_current = solution[5];
        const double secondary_current = solution[6];
        const double supply_current = solution[7];
        const double magnetizing_current = primary_current + secondary_current / turns_ratio;
        const auto core = EvaluateCore(magnetizing_current, state.core.hysteresis,
            magnetizing_inductance, saturation_flux, hysteresis_width, max_flux, transformer_max_current);
        const auto pentode = EvaluatePentode(
            plate, screen, grid, screen_amplification_factor, perveance, knee_voltage,
            screen_current_fraction, max_current);
        const auto supply = EvaluateSupply(
            supply_current, state.supply, timestep_seconds, supply_voltage,
            sag_resistance, recovery_time_seconds, maximum_sag, maximum_load_current);
        const double core_dynamic = core.differential_flux / timestep_seconds;
        Vector8 residual{
            input_conductance * (grid - input) + capacitor_conductance * (grid - state.grid_history),
            pentode.plate + primary_current,
            (screen - rail) / screen_ohms + pentode.screen,
            -primary_current + (rail - screen) / screen_ohms + supply_current,
            secondary_current + secondary / secondary_load_ohms,
            (plate - rail) - primary_resistance * primary_current -
                primary_dynamic * (primary_current - state.primary_current) -
                (core.flux - core_previous_flux) / timestep_seconds,
            secondary - secondary_resistance * secondary_current -
                secondary_dynamic * (secondary_current - state.secondary_current) -
                (core.flux - core_previous_flux) / (turns_ratio * timestep_seconds),
            rail - supply.target_voltage - source_resistance * supply_current};
        Matrix8 jacobian{};
        jacobian[0][0] = input_conductance + capacitor_conductance;
        jacobian[1][0] = pentode.d_plate_d_grid;
        jacobian[1][1] = pentode.d_plate_d_plate;
        jacobian[1][2] = pentode.d_plate_d_screen;
        jacobian[1][5] = 1.0;
        jacobian[2][0] = pentode.d_screen_d_grid;
        jacobian[2][1] = pentode.d_screen_d_plate;
        jacobian[2][2] = 1.0 / screen_ohms + pentode.d_screen_d_screen;
        jacobian[2][3] = -1.0 / screen_ohms;
        jacobian[3][2] = -1.0 / screen_ohms;
        jacobian[3][3] = 1.0 / screen_ohms;
        jacobian[3][5] = -1.0;
        jacobian[3][7] = 1.0;
        jacobian[4][4] = 1.0 / secondary_load_ohms;
        jacobian[4][6] = 1.0;
        jacobian[5][1] = 1.0;
        jacobian[5][3] = -1.0;
        jacobian[5][5] = -primary_resistance - primary_dynamic - core_dynamic;
        jacobian[5][6] = -core_dynamic / turns_ratio;
        jacobian[6][4] = 1.0;
        jacobian[6][5] = -core_dynamic / turns_ratio;
        jacobian[6][6] = -secondary_resistance - secondary_dynamic -
            core_dynamic / (turns_ratio * turns_ratio);
        jacobian[7][3] = 1.0;
        jacobian[7][7] = -source_resistance - supply.target_derivative_current;
        Vector8 negated{};
        for (std::size_t index = 0; index < 8U; ++index) negated[index] = -residual[index];
        Vector8 delta{};
        if (!SolveLinear(jacobian, negated, delta)) throw std::runtime_error("singular M23-C3c reference");
        double largest_delta = 0.0;
        for (std::size_t index = 0; index < 8U; ++index) {
            solution[index] += delta[index];
            largest_delta = std::max(largest_delta, std::fabs(delta[index]));
        }
        if (!std::isfinite(largest_delta)) throw std::runtime_error("non-finite M23-C3c reference");
        if (largest_delta <= 1.0e-12) break;
    }
    state.grid_history = solution[0];
    state.primary_current = solution[5];
    state.secondary_current = solution[6];
    const double current = state.primary_current + state.secondary_current / turns_ratio;
    const double target_hysteresis = hysteresis_width * std::tanh(magnetizing_inductance * current / saturation_flux);
    const double alpha = 1.0 - std::exp(-hysteresis_rate * timestep_seconds);
    state.core.hysteresis = std::clamp(state.core.hysteresis + alpha *
        (target_hysteresis - state.core.hysteresis), -hysteresis_width, hysteresis_width);
    state.core.flux = EvaluateCore(current, state.core.hysteresis, magnetizing_inductance,
        saturation_flux, hysteresis_width, max_flux, transformer_max_current).flux;
    state.supply = EvaluateSupply(solution[7], state.supply, timestep_seconds, supply_voltage,
        sag_resistance, recovery_time_seconds, maximum_sag, maximum_load_current).next_state;
    return solution;
}

std::vector<double> CompositeFormulaOutputs(
    const std::vector<double>& inputs, double input_ohms, double capacitance,
    double initial_grid, double sample_rate, std::uint32_t oversampling,
    double supply_voltage, double source_resistance, double sag_resistance,
    double recovery_time_seconds, double maximum_sag, double maximum_load_current,
    double screen_ohms, double secondary_load_ohms, double screen_amplification_factor,
    double perveance, double knee_voltage, double screen_current_fraction,
    double max_current, double turns_ratio, double magnetizing_inductance,
    double primary_leakage_inductance, double secondary_leakage_inductance,
    double primary_resistance, double secondary_resistance, double saturation_flux,
    double hysteresis_width, double hysteresis_rate, double max_flux,
    double transformer_max_current) {
    CompositeState state{.grid_history = initial_grid};
    const double timestep = 1.0 / (sample_rate * static_cast<double>(oversampling));
    std::vector<double> outputs;
    outputs.reserve(inputs.size());
    for (const double input : inputs) {
        Vector8 solution{};
        for (std::uint32_t substep = 0; substep < oversampling; ++substep) {
            solution = SolveCompositeStep(
                input, timestep, state, input_ohms, capacitance, supply_voltage, source_resistance,
                sag_resistance, recovery_time_seconds, maximum_sag, maximum_load_current,
                screen_ohms, secondary_load_ohms, screen_amplification_factor, perveance,
                knee_voltage, screen_current_fraction, max_current, turns_ratio,
                magnetizing_inductance, primary_leakage_inductance, secondary_leakage_inductance,
                primary_resistance, secondary_resistance, saturation_flux, hysteresis_width,
                hysteresis_rate, max_flux, transformer_max_current);
        }
        outputs.push_back(solution[4]);
    }
    return outputs;
}

std::vector<double> SolveFixture(const std::vector<double>& inputs) {
    const auto prepared = solo_rig::PrepareWhiteboxMnaV2Json(
        ReadFixture("m23c_tube_supply_core_dynamic_circuit.json"), 192000.0);
    REQUIRE(prepared.error == solo_rig::WhiteboxMnaJsonError::None);
    REQUIRE(prepared.circuit.has_value());
    auto circuit = std::move(*prepared.circuit);
    std::vector<double> outputs;
    outputs.reserve(inputs.size());
    for (const double input : inputs) {
        const auto result = circuit.Solve(input);
        REQUIRE(result.Succeeded());
        REQUIRE(std::isfinite(result.output_voltage));
        outputs.push_back(result.output_voltage);
    }
    circuit.Reset();
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        const auto result = circuit.Solve(inputs[index]);
        REQUIRE(result.Succeeded());
        REQUIRE(std::fabs(result.output_voltage - outputs[index]) <= 1.0e-12);
    }
    return outputs;
}

} // namespace

TEST_CASE("M23-C3c pentode dynamic supply and saturating transformer golden matches independent Newton and real MNA") {
    const auto golden = RootObject(ReadFixture("m23c_tube_supply_core_dynamic_golden.json"));
    const auto inputs = Numbers(golden, "inputs");
    const auto expected = Numbers(golden, "expectedOutputs");
    REQUIRE(inputs.size() == expected.size());
    const auto* formula = Required(golden, "formula").AsObject();
    REQUIRE(formula != nullptr);
    const auto value = [&](std::string_view key) { return Number(Required(*formula, key), key); };
    const auto formula_outputs = CompositeFormulaOutputs(
        inputs, value("inputOhms"), value("gridCapacitanceFarads"), value("initialGridVoltage"),
        value("sampleRateHz"), static_cast<std::uint32_t>(value("oversampling")),
        value("supplyVoltage"), value("sourceResistance"), value("sagResistance"),
        value("recoveryTimeSeconds"), value("maximumSag"), value("maximumLoadCurrent"),
        value("screenOhms"), value("secondaryLoadOhms"), value("screenAmplificationFactor"),
        value("perveance"), value("kneeVoltage"), value("screenCurrentFraction"),
        value("maxCurrent"), value("turnsRatio"), value("magnetizingInductance"),
        value("primaryLeakageInductance"), value("secondaryLeakageInductance"),
        value("primaryResistance"), value("secondaryResistance"), value("saturationFlux"),
        value("hysteresisWidth"), value("hysteresisRate"), value("maxFlux"),
        value("transformerMaxCurrent"));
    REQUIRE(solo_rig::CompareEvidenceSamples(formula_outputs, expected, 1.0e-12).passed);
    const auto outputs = SolveFixture(inputs);
    REQUIRE(solo_rig::CompareEvidenceSamples(
        outputs, expected, Number(Required(golden, "absoluteTolerance"), "absoluteTolerance")).passed);
}

TEST_CASE("M23-C3c supply/core evidence package keeps self-authored progress separate from SPICE") {
    const auto parsed = solo_rig::ParseEvidencePackageJson(
        ReadFixture("m23c_tube_supply_core_dynamic_evidence.json"));
    REQUIRE(parsed.error == std::nullopt);
    REQUIRE(parsed.package.has_value());
    REQUIRE(parsed.package->status == solo_rig::EvidencePackageStatus::Blocked);
    REQUIRE(parsed.package->dynamic_response.status == solo_rig::EvidenceSectionStatus::Passed);
    REQUIRE(parsed.package->golden.status == solo_rig::EvidenceSectionStatus::Passed);
    REQUIRE(parsed.package->spice.status == solo_rig::EvidenceSectionStatus::Blocked);
    REQUIRE(parsed.package->golden.sample_count == 7U);
    REQUIRE(solo_rig::EvaluateEvidencePackage(*parsed.package) == solo_rig::EvidenceEvaluation::Blocked);
}
