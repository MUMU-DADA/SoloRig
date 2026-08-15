#include "dsp/generic_vco.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>

namespace {

constexpr double kTolerance = 1.0e-12;

solo_rig::GenericVcoModel Model() {
    return {.waveform = solo_rig::GenericVcoWaveform::Sine,
        .base_frequency_hz = 10.0,
        .minimum_frequency_hz = 1.0,
        .maximum_frequency_hz = 40.0,
        .volts_per_octave = 1.0,
        .reference_voltage = 0.0,
        .output_amplitude = 1.0,
        .output_offset = 0.0,
        .initial_phase_cycles = 0.0,
        .maximum_control_voltage = 8.0};
}

} // namespace

TEST_CASE("M18-D2b1 generic VCO waveforms match their phase definitions") {
    REQUIRE(std::fabs(solo_rig::EvaluateGenericVcoWaveform(
        solo_rig::GenericVcoWaveform::Sine, 0.25) - 1.0) < kTolerance);
    REQUIRE(std::fabs(solo_rig::EvaluateGenericVcoWaveform(
        solo_rig::GenericVcoWaveform::Triangle, 0.0) + 1.0) < kTolerance);
    REQUIRE(std::fabs(solo_rig::EvaluateGenericVcoWaveform(
        solo_rig::GenericVcoWaveform::Triangle, 0.5) - 1.0) < kTolerance);
    REQUIRE(std::fabs(solo_rig::EvaluateGenericVcoWaveform(
        solo_rig::GenericVcoWaveform::Saw, 0.25) + 0.5) < kTolerance);
    REQUIRE(solo_rig::EvaluateGenericVcoWaveform(
        solo_rig::GenericVcoWaveform::Square, 0.49) == 1.0);
    REQUIRE(solo_rig::EvaluateGenericVcoWaveform(
        solo_rig::GenericVcoWaveform::Square, 0.5) == -1.0);
}

TEST_CASE("M18-D2b1 generic VCO follows volts-per-octave and bounded frequency") {
    solo_rig::GenericVcoRuntime runtime;
    REQUIRE(runtime.Prepare(Model(), 100.0, 1U) == solo_rig::GenericVcoError::None);
    double output{};
    REQUIRE(runtime.Process(1.0, output) == solo_rig::GenericVcoError::None);
    REQUIRE(std::fabs(runtime.CurrentFrequencyHz() - 20.0) < kTolerance);
    REQUIRE(std::fabs(runtime.CurrentPhaseCycles() - 0.2) < kTolerance);
    REQUIRE(std::fabs(output - std::sin(0.4 * 3.14159265358979323846)) < kTolerance);

    REQUIRE(runtime.Process(1.0e6, output) == solo_rig::GenericVcoError::None);
    REQUIRE(runtime.CurrentFrequencyHz() == 40.0);
    REQUIRE(runtime.Process(-1.0e6, output) == solo_rig::GenericVcoError::None);
    REQUIRE(runtime.CurrentFrequencyHz() == 1.0);
}

TEST_CASE("M18-D2b1 generic VCO rejects invalid and nonfinite boundaries") {
    auto invalid = Model();
    invalid.volts_per_octave = 0.0;
    REQUIRE(!solo_rig::ValidateGenericVcoModel(invalid));
    invalid = Model();
    invalid.minimum_frequency_hz = 20.0;
    invalid.base_frequency_hz = 10.0;
    REQUIRE(!solo_rig::ValidateGenericVcoModel(invalid));
    invalid = Model();
    invalid.initial_phase_cycles = 1.0;
    REQUIRE(!solo_rig::ValidateGenericVcoModel(invalid));
    invalid = Model();
    invalid.output_offset = std::numeric_limits<double>::infinity();
    REQUIRE(!solo_rig::ValidateGenericVcoModel(invalid));

    solo_rig::GenericVcoRuntime runtime;
    REQUIRE(runtime.Prepare(Model(), 80.0, 1U) ==
            solo_rig::GenericVcoError::InvalidSampleRate);
    REQUIRE(runtime.Prepare(Model(), 100.0, 0U) ==
            solo_rig::GenericVcoError::InvalidOversampling);
}
