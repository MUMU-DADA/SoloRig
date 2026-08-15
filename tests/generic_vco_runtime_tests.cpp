#include "dsp/generic_vco.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>
#include <type_traits>

namespace {

solo_rig::GenericVcoModel RuntimeModel() {
    return {.waveform = solo_rig::GenericVcoWaveform::Triangle,
        .base_frequency_hz = 1000.0,
        .minimum_frequency_hz = 10.0,
        .maximum_frequency_hz = 20000.0,
        .volts_per_octave = 1.0,
        .reference_voltage = 0.0,
        .output_amplitude = 2.0,
        .output_offset = 0.5,
        .initial_phase_cycles = 0.25,
        .maximum_control_voltage = 10.0};
}

} // namespace

TEST_CASE("M18-D2b1 generic VCO runtime is finite at 192 kHz 4x and Reset-safe") {
    solo_rig::GenericVcoRuntime runtime;
    REQUIRE(runtime.Prepare(RuntimeModel(), 192000.0, 4U) == solo_rig::GenericVcoError::None);
    REQUIRE(runtime.IsPrepared());
    double output{};
    for (int index = 0; index < 4096; ++index) {
        REQUIRE(runtime.Process(index % 2 == 0 ? 0.5 : -0.5, output) ==
                solo_rig::GenericVcoError::None);
        REQUIRE(std::isfinite(output));
        REQUIRE(output >= -1.5 && output <= 2.5);
        REQUIRE(runtime.CurrentPhaseCycles() >= 0.0);
        REQUIRE(runtime.CurrentPhaseCycles() < 1.0);
    }
    runtime.Reset();
    REQUIRE(runtime.CurrentPhaseCycles() == 0.25);
    REQUIRE(runtime.SetSampleRate(96000.0) == solo_rig::GenericVcoError::None);
    REQUIRE(runtime.CurrentPhaseCycles() == 0.25);
    static_assert(noexcept(std::declval<solo_rig::GenericVcoRuntime&>().Process(
        0.0, std::declval<double&>())));
}

TEST_CASE("M18-D2b1 generic VCO runtime has explicit failure state") {
    solo_rig::GenericVcoRuntime runtime;
    double output{123.0};
    REQUIRE(runtime.Process(0.0, output) == solo_rig::GenericVcoError::NotPrepared);
    REQUIRE(output == 0.0);
    REQUIRE(runtime.Prepare(RuntimeModel(), 48000.0, 1U) == solo_rig::GenericVcoError::None);
    REQUIRE(runtime.Process(std::numeric_limits<double>::quiet_NaN(), output) ==
            solo_rig::GenericVcoError::NonFinite);
    REQUIRE(runtime.SetSampleRate(20000.0) == solo_rig::GenericVcoError::InvalidSampleRate);
    REQUIRE(runtime.Process(0.0, output) == solo_rig::GenericVcoError::None);
}
