#include "content/behavior_mna_modulation.hpp"
#include "test_framework.hpp"

#include "../fixtures/whitebox/m19c_representative_pedal_fixtures.hpp"
#include "content/circuit_v2.hpp"
#include "dsp/mna_solver.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

solo_rig::MnaPreparedCircuit PreparedTone() {
    auto circuit = solo_rig::m19c_fixtures::PassiveTone(4U, 0.5);
    circuit.parameter_bindings = {{.model_version = 1U, .parameter_id = "wah",
        .target_id = "tone_pot",
        .property = solo_rig::CircuitParameterBindingProperty::PotentiometerPosition,
        .smoothing_seconds = 0.0}};
    const auto compiled = solo_rig::CompileMnaCircuit(circuit);
    REQUIRE(compiled.circuit.has_value());
    return std::move(*compiled.circuit);
}

solo_rig::TypedLfoMnaModel WahModel() {
    solo_rig::TypedLfoMnaModel model{};
    model.lfo.frequency_hz = 2.0;
    model.lfo.output_amplitude = 1.0;
    model.modulation_min = 0.0;
    model.modulation_max = 1.0;
    return model;
}

} // namespace

TEST_CASE("M20-B2 caller-owned LFO controls a prepared MNA tone network") {
    solo_rig::TypedLfoMnaRuntime runtime;
    REQUIRE(runtime.Prepare(PreparedTone(), "wah", WahModel(), 192000.0, 4U) ==
        solo_rig::TypedLfoMnaRuntimeError::None);
    REQUIRE(runtime.IsPrepared());
    REQUIRE(runtime.ControlIndex() == 0U);
    double first{};
    REQUIRE(runtime.Process(0.25, first) == solo_rig::TypedLfoMnaRuntimeError::None);
    REQUIRE(std::isfinite(first));
    REQUIRE(runtime.CurrentModulationValue() >= 0.0);
    REQUIRE(runtime.CurrentModulationValue() <= 1.0);
    REQUIRE(runtime.SetSampleRate(96000.0) == solo_rig::TypedLfoMnaRuntimeError::None);
    static_assert(noexcept(std::declval<solo_rig::TypedLfoMnaRuntime&>().Process(
        0.0, std::declval<double&>())));
}

TEST_CASE("M20-B2 LFO/MNA composition is deterministic and rejects missing or invalid controls") {
    solo_rig::TypedLfoMnaRuntime runtime;
    REQUIRE(runtime.Prepare(PreparedTone(), "missing", WahModel(), 48000.0, 1U) ==
        solo_rig::TypedLfoMnaRuntimeError::MissingControl);
    auto invalid = WahModel();
    invalid.modulation_min = 1.0;
    invalid.modulation_max = 0.0;
    REQUIRE(runtime.Prepare(PreparedTone(), "wah", invalid, 48000.0, 1U) ==
        solo_rig::TypedLfoMnaRuntimeError::InvalidModel);
    REQUIRE(runtime.Prepare(PreparedTone(), "wah", WahModel(), 48000.0, 1U) ==
        solo_rig::TypedLfoMnaRuntimeError::None);
    std::array<double, 8U> first{};
    for (std::size_t index = 0U; index < first.size(); ++index) {
        REQUIRE(runtime.Process(index == 0U ? 0.25 : -0.1, first[index]) ==
            solo_rig::TypedLfoMnaRuntimeError::None);
    }
    runtime.Reset();
    for (std::size_t index = 0U; index < first.size(); ++index) {
        double output{};
        REQUIRE(runtime.Process(index == 0U ? 0.25 : -0.1, output) ==
            solo_rig::TypedLfoMnaRuntimeError::None);
        REQUIRE(output == first[index]);
    }
    double output{};
    REQUIRE(runtime.Process(std::numeric_limits<double>::quiet_NaN(), output) ==
        solo_rig::TypedLfoMnaRuntimeError::NonFinite);
    REQUIRE(runtime.SetSampleRate(std::numeric_limits<double>::infinity()) ==
        solo_rig::TypedLfoMnaRuntimeError::InvalidSampleRate);
}

TEST_CASE("M20-B3 caller-owned LFO updates two MNA phase-shift controls") {
    auto circuit = solo_rig::m19c_fixtures::PhaserTone(4U);
    const auto compiled = solo_rig::CompileMnaCircuit(circuit);
    REQUIRE(compiled.circuit.has_value());
    const std::array<std::string_view, 2U> parameters{"stage1", "stage2"};
    solo_rig::TypedLfoMnaRuntime runtime;
    REQUIRE(runtime.Prepare(std::move(*compiled.circuit), parameters, WahModel(),
        192000.0, 4U) == solo_rig::TypedLfoMnaRuntimeError::None);
    REQUIRE(runtime.ControlCount() == 2U);
    REQUIRE(runtime.ControlIndex(0U) == 0U);
    REQUIRE(runtime.ControlIndex(1U) == 1U);
    double first{};
    REQUIRE(runtime.Process(0.3, first) == solo_rig::TypedLfoMnaRuntimeError::None);
    REQUIRE(std::isfinite(first));
    double second{};
    REQUIRE(runtime.Process(-0.3, second) == solo_rig::TypedLfoMnaRuntimeError::None);
    REQUIRE(std::isfinite(second));
    runtime.Reset();
    double reset{};
    REQUIRE(runtime.Process(0.3, reset) == solo_rig::TypedLfoMnaRuntimeError::None);
    REQUIRE(reset == first);
}
