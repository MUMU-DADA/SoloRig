#include "dsp/generic_lfo.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>

namespace {

solo_rig::GenericLfoModel Model(solo_rig::GenericLfoWaveform waveform) {
    return {.waveform = waveform,
        .frequency_hz = 1.0,
        .output_amplitude = 1.0,
        .output_offset = 0.0,
        .maximum_output = 2.0,
        .initial_phase_cycles = 0.0,
        .duty_cycle = 0.25};
}

double Output(const solo_rig::GenericLfoModel& model, double phase) {
    double output{};
    REQUIRE(solo_rig::EvaluateGenericLfoOutput(model, phase, output) ==
            solo_rig::GenericLfoError::None);
    return output;
}

} // namespace

TEST_CASE("M18-D2c2 generic LFO waveforms match their phase definitions") {
    REQUIRE(std::fabs(Output(Model(solo_rig::GenericLfoWaveform::Sine), 0.25) - 1.0) < 1.0e-12);
    REQUIRE(Output(Model(solo_rig::GenericLfoWaveform::Triangle), 0.0) == -1.0);
    REQUIRE(Output(Model(solo_rig::GenericLfoWaveform::Triangle), 0.5) == 1.0);
    REQUIRE(Output(Model(solo_rig::GenericLfoWaveform::Saw), 0.25) == -0.5);
    REQUIRE(Output(Model(solo_rig::GenericLfoWaveform::Square), 0.24) == 1.0);
    REQUIRE(Output(Model(solo_rig::GenericLfoWaveform::Square), 0.25) == -1.0);
    REQUIRE(std::fabs(Output(Model(solo_rig::GenericLfoWaveform::Sine), 1.25) - 1.0) < 1.0e-12);
}

TEST_CASE("M18-D2c2 generic LFO applies amplitude offset and output clamp") {
    auto model = Model(solo_rig::GenericLfoWaveform::Sine);
    model.output_amplitude = 2.0;
    model.output_offset = 0.5;
    model.maximum_output = 1.0;
    REQUIRE(Output(model, 0.25) == 1.0);
    REQUIRE(Output(model, 0.75) == -1.0);
}

TEST_CASE("M18-D2c2 generic LFO rejects invalid and nonfinite contracts") {
    auto invalid = Model(solo_rig::GenericLfoWaveform::Sine);
    invalid.frequency_hz = 0.0;
    REQUIRE(!solo_rig::ValidateGenericLfoModel(invalid));
    invalid = Model(solo_rig::GenericLfoWaveform::Sine);
    invalid.duty_cycle = 1.0;
    REQUIRE(!solo_rig::ValidateGenericLfoModel(invalid));
    invalid = Model(solo_rig::GenericLfoWaveform::Sine);
    invalid.initial_phase_cycles = -0.1;
    REQUIRE(!solo_rig::ValidateGenericLfoModel(invalid));
    invalid = Model(solo_rig::GenericLfoWaveform::Sine);
    invalid.output_offset = std::numeric_limits<double>::infinity();
    REQUIRE(!solo_rig::ValidateGenericLfoModel(invalid));

    double output{123.0};
    REQUIRE(solo_rig::EvaluateGenericLfoOutput(
        Model(solo_rig::GenericLfoWaveform::Sine),
        std::numeric_limits<double>::quiet_NaN(), output) ==
        solo_rig::GenericLfoError::NonFinite);
    REQUIRE(output == 0.0);
}
