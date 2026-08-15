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

using Vector7 = std::array<double, 7U>;
using Matrix7 = std::array<std::array<double, 7U>, 7U>;

std::string ReadFixture(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" / "whitebox" / name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open M23-C3b composite fixture: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

solo_rig::JsonValue::Object RootObject(std::string_view source) {
    const auto parsed = solo_rig::ParseJson(source);
    if (!parsed.value.has_value() || parsed.value->AsObject() == nullptr) {
        throw std::runtime_error("invalid M23-C3b composite golden JSON");
    }
    return *parsed.value->AsObject();
}

const solo_rig::JsonValue& Required(const solo_rig::JsonValue::Object& object, std::string_view key) {
    const auto found = object.find(key);
    if (found == object.end()) throw std::runtime_error("missing M23-C3b composite field: " + std::string(key));
    return found->second;
}

double Number(const solo_rig::JsonValue& value, std::string_view field) {
    const auto number = value.AsNumber();
    if (number == nullptr || !std::isfinite(*number)) {
        throw std::runtime_error("invalid M23-C3b composite number: " + std::string(field));
    }
    return *number;
}

std::vector<double> Numbers(const solo_rig::JsonValue::Object& object, std::string_view key) {
    const auto* array = Required(object, key).AsArray();
    if (array == nullptr || array->empty()) throw std::runtime_error("invalid M23-C3b composite array");
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
    result.d_plate_d_plate = plate_fraction * space_current * derivative_share;
    result.d_plate_d_screen = plate_fraction * derivative_control /
        screen_amplification_factor * plate_share;
    result.d_plate_d_grid = plate_fraction * derivative_control * plate_share;
    result.d_screen_d_plate = 0.0;
    result.d_screen_d_screen = screen_current_fraction * derivative_control /
        screen_amplification_factor;
    result.d_screen_d_grid = screen_current_fraction * derivative_control;
    return result;
}

double SechSquared(double value) noexcept {
    const double tangent = std::tanh(value);
    return 1.0 - tangent * tangent;
}

struct SpeakerState {
    double current{};
    double velocity{};
    double displacement{};
};

struct SpeakerEvaluation {
    double voltage{};
    double voltage_derivative_current{};
    SpeakerState next_state{};
};

SpeakerEvaluation EvaluateSpeaker(
    double branch_current, const SpeakerState& previous_state, double timestep_seconds,
    double voice_coil_resistance, double voice_coil_inductance, double force_factor,
    double moving_mass, double mechanical_resistance, double compliance,
    double nonlinear_stiffness, double nonlinear_displacement_scale,
    double maximum_current, double maximum_velocity, double maximum_displacement) {
    const double normalized_current = branch_current / maximum_current;
    const double effective_current = maximum_current * std::tanh(normalized_current);
    const double current_derivative = SechSquared(normalized_current);
    const double mass_over_dt = moving_mass / timestep_seconds;
    const double linear_stiffness = 1.0 / compliance;
    const auto mechanics = [&](double velocity) noexcept {
        const double raw_displacement = previous_state.displacement + timestep_seconds * velocity;
        const double displacement = std::clamp(raw_displacement, -maximum_displacement, maximum_displacement);
        const double displacement_derivative_velocity =
            raw_displacement > -maximum_displacement && raw_displacement < maximum_displacement
                ? timestep_seconds : 0.0;
        const double normalized_displacement = displacement / nonlinear_displacement_scale;
        const double spring_force = linear_stiffness * displacement +
            nonlinear_stiffness * nonlinear_displacement_scale * std::tanh(normalized_displacement);
        const double spring_derivative = linear_stiffness +
            nonlinear_stiffness * SechSquared(normalized_displacement);
        const double residual = mass_over_dt * (velocity - previous_state.velocity) +
            mechanical_resistance * velocity + spring_force - force_factor * effective_current;
        const double derivative = mass_over_dt + mechanical_resistance +
            spring_derivative * displacement_derivative_velocity;
        return std::array<double, 4>{residual, derivative, displacement, displacement_derivative_velocity};
    };

    double lower = -maximum_velocity;
    double upper = maximum_velocity;
    const auto lower_evaluation = mechanics(lower);
    const auto upper_evaluation = mechanics(upper);
    double velocity{};
    bool velocity_clamped{};
    if (lower_evaluation[0] >= 0.0) {
        velocity = lower;
        velocity_clamped = true;
    } else if (upper_evaluation[0] <= 0.0) {
        velocity = upper;
        velocity_clamped = true;
    } else {
        velocity = std::clamp(previous_state.velocity, lower, upper);
        for (int iteration = 0; iteration < 48; ++iteration) {
            const auto evaluated = mechanics(velocity);
            if (evaluated[0] > 0.0) upper = velocity;
            else lower = velocity;
            if (std::fabs(evaluated[0]) <= 1.0e-12 ||
                upper - lower <= 1.0e-14 * (1.0 + std::fabs(velocity))) break;
            const double candidate = velocity - evaluated[0] / evaluated[1];
            velocity = candidate > lower && candidate < upper ? candidate : 0.5 * (lower + upper);
        }
    }

    const auto final_mechanics = mechanics(velocity);
    const double velocity_derivative_current = velocity_clamped ? 0.0 :
        force_factor * current_derivative / final_mechanics[1];
    const double inductance_over_dt = voice_coil_inductance / timestep_seconds;
    return {
        .voltage = voice_coil_resistance * effective_current +
            inductance_over_dt * (effective_current - previous_state.current) + force_factor * velocity,
        .voltage_derivative_current =
            (voice_coil_resistance + inductance_over_dt) * current_derivative +
            force_factor * velocity_derivative_current,
        .next_state = {.current = effective_current, .velocity = velocity,
                       .displacement = final_mechanics[2]}};
}

bool SolveLinear(Matrix7 matrix, Vector7 rhs, Vector7& result) {
    for (std::size_t pivot = 0; pivot < 7U; ++pivot) {
        std::size_t best = pivot;
        for (std::size_t row = pivot + 1U; row < 7U; ++row) {
            if (std::fabs(matrix[row][pivot]) > std::fabs(matrix[best][pivot])) best = row;
        }
        if (!std::isfinite(matrix[best][pivot]) || std::fabs(matrix[best][pivot]) <= 1.0e-18) return false;
        if (best != pivot) {
            std::swap(matrix[best], matrix[pivot]);
            std::swap(rhs[best], rhs[pivot]);
        }
        for (std::size_t row = pivot + 1U; row < 7U; ++row) {
            const double factor = matrix[row][pivot] / matrix[pivot][pivot];
            for (std::size_t column = pivot; column < 7U; ++column) {
                matrix[row][column] -= factor * matrix[pivot][column];
            }
            rhs[row] -= factor * rhs[pivot];
        }
    }
    for (std::size_t reverse = 0; reverse < 7U; ++reverse) {
        const std::size_t row = 6U - reverse;
        double value = rhs[row];
        for (std::size_t column = row + 1U; column < 7U; ++column) {
            value -= matrix[row][column] * result[column];
        }
        result[row] = value / matrix[row][row];
    }
    return std::all_of(result.begin(), result.end(), [](double value) { return std::isfinite(value); });
}

struct CompositeState {
    double grid_history{-1.0};
    double primary_current{};
    double secondary_current{};
    SpeakerState speaker{};
};

Vector7 SolveCompositeStep(
    double input, double timestep_seconds, CompositeState& state,
    double input_ohms, double capacitance, double supply, double screen_ohms,
    double screen_amplification_factor, double perveance, double knee_voltage,
    double screen_current_fraction, double max_current, double turns_ratio,
    double magnetizing_inductance, double primary_leakage_inductance,
    double secondary_leakage_inductance, double primary_resistance,
    double secondary_resistance, double voice_coil_resistance,
    double voice_coil_inductance, double force_factor, double moving_mass,
    double mechanical_resistance, double compliance, double nonlinear_stiffness,
    double nonlinear_displacement_scale, double maximum_current,
    double maximum_velocity, double maximum_displacement) {
    const double input_conductance = 1.0 / input_ohms;
    const double capacitor_conductance = capacitance / timestep_seconds;
    const double primary_inductance = magnetizing_inductance + primary_leakage_inductance;
    const double secondary_inductance = magnetizing_inductance / (turns_ratio * turns_ratio) +
        secondary_leakage_inductance;
    const double mutual_inductance = magnetizing_inductance / turns_ratio;
    const double primary_dynamic = primary_inductance / timestep_seconds;
    const double secondary_dynamic = secondary_inductance / timestep_seconds;
    const double mutual_dynamic = mutual_inductance / timestep_seconds;

    Vector7 solution{state.grid_history, supply, supply, 0.0,
                     state.primary_current, state.secondary_current, state.speaker.current};
    for (int iteration = 0; iteration < 128; ++iteration) {
        const double grid = solution[0];
        const double plate = solution[1];
        const double screen = solution[2];
        const double secondary = solution[3];
        const double primary_current = solution[4];
        const double secondary_current = solution[5];
        const double speaker_current = solution[6];
        const auto pentode = EvaluatePentode(
            plate, screen, grid, screen_amplification_factor, perveance, knee_voltage,
            screen_current_fraction, max_current);
        const auto speaker = EvaluateSpeaker(
            speaker_current, state.speaker, timestep_seconds, voice_coil_resistance,
            voice_coil_inductance, force_factor, moving_mass, mechanical_resistance,
            compliance, nonlinear_stiffness, nonlinear_displacement_scale,
            maximum_current, maximum_velocity, maximum_displacement);
        Vector7 residual{
            input_conductance * (grid - input) + capacitor_conductance * (grid - state.grid_history),
            pentode.plate + primary_current,
            (screen - supply) / screen_ohms + pentode.screen,
            secondary_current + speaker_current,
            (plate - supply) - primary_resistance * primary_current -
                primary_dynamic * (primary_current - state.primary_current) -
                mutual_dynamic * (secondary_current - state.secondary_current),
            secondary - secondary_resistance * secondary_current -
                secondary_dynamic * (secondary_current - state.secondary_current) -
                mutual_dynamic * (primary_current - state.primary_current),
            secondary - speaker.voltage};
        Matrix7 jacobian{};
        jacobian[0][0] = input_conductance + capacitor_conductance;
        jacobian[1][0] = pentode.d_plate_d_grid;
        jacobian[1][1] = pentode.d_plate_d_plate;
        jacobian[1][2] = pentode.d_plate_d_screen;
        jacobian[1][4] = 1.0;
        jacobian[2][0] = pentode.d_screen_d_grid;
        jacobian[2][1] = pentode.d_screen_d_plate;
        jacobian[2][2] = 1.0 / screen_ohms + pentode.d_screen_d_screen;
        jacobian[3][5] = 1.0;
        jacobian[3][6] = 1.0;
        jacobian[4][1] = 1.0;
        jacobian[4][4] = -primary_resistance - primary_dynamic;
        jacobian[4][5] = -mutual_dynamic;
        jacobian[5][3] = 1.0;
        jacobian[5][4] = -mutual_dynamic;
        jacobian[5][5] = -secondary_resistance - secondary_dynamic;
        jacobian[6][3] = 1.0;
        jacobian[6][6] = -speaker.voltage_derivative_current;

        Vector7 delta{};
        Vector7 negated{};
        for (std::size_t index = 0; index < 7U; ++index) negated[index] = -residual[index];
        if (!SolveLinear(jacobian, negated, delta)) throw std::runtime_error("singular M23-C3b reference");
        double largest_delta = 0.0;
        for (std::size_t index = 0; index < 7U; ++index) {
            solution[index] += delta[index];
            largest_delta = std::max(largest_delta, std::fabs(delta[index]));
        }
        if (!std::isfinite(largest_delta)) throw std::runtime_error("non-finite M23-C3b reference");
        if (largest_delta <= 1.0e-12) break;
    }
    state.primary_current = solution[4];
    state.secondary_current = solution[5];
    state.speaker = EvaluateSpeaker(
        solution[6], state.speaker, timestep_seconds, voice_coil_resistance,
        voice_coil_inductance, force_factor, moving_mass, mechanical_resistance,
        compliance, nonlinear_stiffness, nonlinear_displacement_scale,
        maximum_current, maximum_velocity, maximum_displacement).next_state;
    return solution;
}

std::vector<double> CompositeFormulaOutputs(
    const std::vector<double>& inputs, double input_ohms, double capacitance,
    double initial_grid, double sample_rate, std::uint32_t oversampling,
    double supply, double screen_ohms, double screen_amplification_factor,
    double perveance, double knee_voltage, double screen_current_fraction,
    double max_current, double turns_ratio, double magnetizing_inductance,
    double primary_leakage_inductance, double secondary_leakage_inductance,
    double primary_resistance, double secondary_resistance,
    double voice_coil_resistance, double voice_coil_inductance, double force_factor,
    double moving_mass, double mechanical_resistance, double compliance,
    double nonlinear_stiffness, double nonlinear_displacement_scale,
    double maximum_current, double maximum_velocity, double maximum_displacement) {
    CompositeState state{.grid_history = initial_grid};
    const double timestep = 1.0 / (sample_rate * static_cast<double>(oversampling));
    std::vector<double> outputs;
    outputs.reserve(inputs.size());
    for (const double input : inputs) {
        Vector7 solution{};
        for (std::uint32_t substep = 0; substep < oversampling; ++substep) {
            solution = SolveCompositeStep(
                input, timestep, state, input_ohms, capacitance, supply, screen_ohms,
                screen_amplification_factor, perveance, knee_voltage,
                screen_current_fraction, max_current, turns_ratio,
                magnetizing_inductance, primary_leakage_inductance,
                secondary_leakage_inductance, primary_resistance, secondary_resistance,
                voice_coil_resistance, voice_coil_inductance, force_factor, moving_mass,
                mechanical_resistance, compliance, nonlinear_stiffness,
                nonlinear_displacement_scale, maximum_current, maximum_velocity,
                maximum_displacement);
            state.grid_history = (input / input_ohms + capacitance * sample_rate *
                static_cast<double>(oversampling) * state.grid_history) /
                (1.0 / input_ohms + capacitance * sample_rate * static_cast<double>(oversampling));
        }
        outputs.push_back(solution[3]);
    }
    return outputs;
}

std::vector<double> SolveFixture(const std::vector<double>& inputs) {
    const auto prepared = solo_rig::PrepareWhiteboxMnaV2Json(
        ReadFixture("m23c_tube_power_load_dynamic_circuit.json"), 192000.0);
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
        REQUIRE(std::isfinite(result.output_voltage));
        REQUIRE(std::fabs(result.output_voltage - outputs[index]) <= 1.0e-12);
    }
    return outputs;
}

} // namespace

TEST_CASE("M23-C3b pentode transformer speaker golden matches an independent composite Newton solve and real MNA") {
    const auto golden = RootObject(ReadFixture("m23c_tube_power_load_dynamic_golden.json"));
    const auto inputs = Numbers(golden, "inputs");
    const auto expected = Numbers(golden, "expectedOutputs");
    REQUIRE(inputs.size() == expected.size());
    const auto* formula = Required(golden, "formula").AsObject();
    REQUIRE(formula != nullptr);
    const auto value = [&](std::string_view key) { return Number(Required(*formula, key), key); };
    const auto formula_outputs = CompositeFormulaOutputs(
        inputs, value("inputOhms"), value("gridCapacitanceFarads"), value("initialGridVoltage"),
        value("sampleRateHz"), static_cast<std::uint32_t>(value("oversampling")),
        value("supplyVoltage"), value("screenOhms"), value("screenAmplificationFactor"),
        value("perveance"), value("kneeVoltage"), value("screenCurrentFraction"),
        value("maxCurrent"), value("turnsRatio"), value("magnetizingInductance"),
        value("primaryLeakageInductance"), value("secondaryLeakageInductance"),
        value("primaryResistance"), value("secondaryResistance"),
        value("voiceCoilResistance"), value("voiceCoilInductance"), value("forceFactor"),
        value("movingMass"), value("mechanicalResistance"), value("compliance"),
        value("nonlinearStiffness"), value("nonlinearDisplacementScale"),
        value("maximumCurrent"), value("maximumVelocity"), value("maximumDisplacement"));
    REQUIRE(solo_rig::CompareEvidenceSamples(formula_outputs, expected, 1.0e-12).passed);

    const auto outputs = SolveFixture(inputs);
    const auto comparison = solo_rig::CompareEvidenceSamples(
        outputs, expected, Number(Required(golden, "absoluteTolerance"), "absoluteTolerance"));
    REQUIRE(comparison.passed);
}

TEST_CASE("M23-C3b composite evidence package keeps self-authored power-load progress separate from SPICE") {
    const auto parsed = solo_rig::ParseEvidencePackageJson(
        ReadFixture("m23c_tube_power_load_dynamic_evidence.json"));
    REQUIRE(parsed.error == std::nullopt);
    REQUIRE(parsed.package.has_value());
    REQUIRE(parsed.package->status == solo_rig::EvidencePackageStatus::Blocked);
    REQUIRE(parsed.package->dynamic_response.status == solo_rig::EvidenceSectionStatus::Passed);
    REQUIRE(parsed.package->golden.status == solo_rig::EvidenceSectionStatus::Passed);
    REQUIRE(parsed.package->spice.status == solo_rig::EvidenceSectionStatus::Blocked);
    REQUIRE(parsed.package->golden.sample_count == 7U);
    REQUIRE(solo_rig::EvaluateEvidencePackage(*parsed.package) == solo_rig::EvidenceEvaluation::Blocked);
}
