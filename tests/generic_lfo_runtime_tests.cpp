#include "dsp/generic_lfo.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>
#include <type_traits>

namespace {

solo_rig::GenericLfoModel Model() {
    return {.waveform = solo_rig::GenericLfoWaveform::Sine,
        .frequency_hz = 1.0,
        .output_amplitude = 1.0,
        .output_offset = 0.0,
        .maximum_output = 1.0,
        .initial_phase_cycles = 0.0,
        .duty_cycle = 0.5};
}

} // namespace

TEST_CASE("M18-D2c2 generic LFO runtime advances phase at the declared frequency") {
    solo_rig::GenericLfoRuntime runtime;
    REQUIRE(runtime.Prepare(Model(), 4.0, 1U) == solo_rig::GenericLfoError::None);
    double output{};
    REQUIRE(runtime.Process(output) == solo_rig::GenericLfoError::None);
    REQUIRE(std::fabs(runtime.CurrentPhaseCycles() - 0.25) < 1.0e-12);
    REQUIRE(std::fabs(output - 1.0) < 1.0e-12);
    REQUIRE(runtime.Process(output) == solo_rig::GenericLfoError::None);
    REQUIRE(std::fabs(runtime.CurrentPhaseCycles() - 0.5) < 1.0e-12);
    REQUIRE(std::fabs(output) < 1.0e-12);
}

TEST_CASE("M18-D2c2 generic LFO advances every oversampling substep") {
    solo_rig::GenericLfoRuntime runtime;
    REQUIRE(runtime.Prepare(Model(), 4.0, 4U) == solo_rig::GenericLfoError::None);
    double output{};
    REQUIRE(runtime.Process(output) == solo_rig::GenericLfoError::None);
    REQUIRE(std::fabs(runtime.CurrentPhaseCycles() - 0.25) < 1.0e-12);
    REQUIRE(std::fabs(output - 1.0) < 1.0e-12);
}

TEST_CASE("M18-D2c2 generic LFO Reset and sample-rate boundaries are explicit") {
    auto model = Model();
    model.initial_phase_cycles = 0.25;
    solo_rig::GenericLfoRuntime runtime;
    REQUIRE(runtime.Prepare(model, 48000.0, 1U) == solo_rig::GenericLfoError::None);
    double output{};
    REQUIRE(runtime.Process(output) == solo_rig::GenericLfoError::None);
    runtime.Reset();
    REQUIRE(runtime.CurrentPhaseCycles() == 0.25);
    REQUIRE(runtime.SetSampleRate(96000.0) == solo_rig::GenericLfoError::None);
    REQUIRE(runtime.CurrentPhaseCycles() == 0.25);
    REQUIRE(runtime.SetSampleRate(std::numeric_limits<double>::quiet_NaN()) ==
            solo_rig::GenericLfoError::InvalidSampleRate);

    auto nyquist = Model();
    nyquist.frequency_hz = 100.0;
    REQUIRE(runtime.Prepare(nyquist, 200.0, 1U) ==
            solo_rig::GenericLfoError::InvalidSampleRate);
    REQUIRE(runtime.Prepare(nyquist, 201.0, 1U) == solo_rig::GenericLfoError::None);
}

TEST_CASE("M18-D2c2 generic LFO runtime is finite at 192 kHz 4x") {
    auto model = Model();
    model.frequency_hz = 20.0;
    solo_rig::GenericLfoRuntime runtime;
    REQUIRE(runtime.Prepare(model, 192000.0, 4U) == solo_rig::GenericLfoError::None);
    double output{};
    for (int index = 0; index < 4096; ++index) {
        REQUIRE(runtime.Process(output) == solo_rig::GenericLfoError::None);
        REQUIRE(std::isfinite(output));
        REQUIRE(runtime.CurrentPhaseCycles() >= 0.0);
        REQUIRE(runtime.CurrentPhaseCycles() < 1.0);
    }
    static_assert(noexcept(std::declval<solo_rig::GenericLfoRuntime&>().Process(
        std::declval<double&>())));
}

TEST_CASE("M18-D2c2 generic LFO runtime rejects unsafe preparation") {
    solo_rig::GenericLfoRuntime runtime;
    double output{123.0};
    REQUIRE(runtime.Process(output) == solo_rig::GenericLfoError::NotPrepared);
    REQUIRE(output == 0.0);
    REQUIRE(runtime.Prepare(Model(), 48000.0, 0U) ==
            solo_rig::GenericLfoError::InvalidOversampling);
    REQUIRE(runtime.Prepare(Model(), 48000.0, 17U) ==
            solo_rig::GenericLfoError::InvalidOversampling);
}
