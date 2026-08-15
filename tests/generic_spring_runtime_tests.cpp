#include "dsp/generic_spring.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

namespace {

solo_rig::GenericSpringModel Model() {
    return {.mode_count = 1U,
        .base_frequency_hz = 25.0,
        .mode_spacing_hz = 0.0,
        .dispersion = 0.0,
        .decay_seconds = 1.0 / (100.0 * 0.69314718055994530942),
        .high_frequency_damping = 0.0,
        .drive_gain = 1.0,
        .pickup_gain = 1.0,
        .wet = 1.0,
        .dry = 0.0,
        .maximum_input = 1.0,
        .maximum_output = 10.0};
}

} // namespace

TEST_CASE("M18-D2d2 generic spring single mode matches the declared rotation reference") {
    solo_rig::GenericSpringRuntime runtime;
    REQUIRE(runtime.Prepare(Model(), 100.0, 1U) == solo_rig::GenericSpringError::None);
    double output{};
    REQUIRE(runtime.Process(1.0, output) == solo_rig::GenericSpringError::None);
    REQUIRE(std::fabs(output - 0.5) < 1.0e-12);
    REQUIRE(runtime.Process(0.0, output) == solo_rig::GenericSpringError::None);
    REQUIRE(std::fabs(output) < 1.0e-12);
    REQUIRE(runtime.Process(0.0, output) == solo_rig::GenericSpringError::None);
    REQUIRE(std::fabs(output + 0.125) < 1.0e-12);
    REQUIRE(runtime.CurrentStateEnergy() > 0.0);
}

TEST_CASE("M18-D2d2 generic spring advances every oversampling substep") {
    solo_rig::GenericSpringRuntime runtime;
    REQUIRE(runtime.Prepare(Model(), 25.0, 4U) == solo_rig::GenericSpringError::None);
    double output{};
    REQUIRE(runtime.Process(1.0, output) == solo_rig::GenericSpringError::None);
    REQUIRE(std::fabs(output - 0.375) < 1.0e-12);
}

TEST_CASE("M18-D2d2 generic spring applies gains mix clamps and Reset") {
    auto model = Model();
    model.drive_gain = 2.0;
    model.pickup_gain = 3.0;
    model.wet = 0.0;
    model.dry = 0.5;
    model.maximum_input = 2.0;
    model.maximum_output = 0.75;
    solo_rig::GenericSpringRuntime runtime;
    REQUIRE(runtime.Prepare(model, 100.0, 1U) == solo_rig::GenericSpringError::None);
    double output{};
    REQUIRE(runtime.Process(100.0, output) == solo_rig::GenericSpringError::None);
    REQUIRE(output == 0.75);
    REQUIRE(runtime.CurrentStateEnergy() > 0.0);
    runtime.Reset();
    REQUIRE(runtime.CurrentStateEnergy() == 0.0);
    REQUIRE(runtime.Process(0.0, output) == solo_rig::GenericSpringError::None);
    REQUIRE(output == 0.0);
    REQUIRE(runtime.SetSampleRate(200.0) == solo_rig::GenericSpringError::None);
    REQUIRE(runtime.CurrentStateEnergy() == 0.0);
}

TEST_CASE("M18-D2d2 generic spring multimode response rings and decays") {
    auto model = Model();
    model.mode_count = 8U;
    model.base_frequency_hz = 80.0;
    model.mode_spacing_hz = 55.0;
    model.dispersion = 0.5;
    model.decay_seconds = 0.05;
    model.high_frequency_damping = 8.0;
    solo_rig::GenericSpringRuntime runtime;
    REQUIRE(runtime.Prepare(model, 4000.0, 1U) == solo_rig::GenericSpringError::None);
    double output{};
    REQUIRE(runtime.Process(1.0, output) == solo_rig::GenericSpringError::None);
    const double initial_energy = runtime.CurrentStateEnergy();
    REQUIRE(initial_energy > 0.0);
    double peak = std::fabs(output);
    for (int index = 0; index < 4000; ++index) {
        REQUIRE(runtime.Process(0.0, output) == solo_rig::GenericSpringError::None);
        peak = std::max(peak, std::fabs(output));
    }
    REQUIRE(peak > 0.0);
    REQUIRE(runtime.CurrentStateEnergy() < initial_energy * 1.0e-6);
}

TEST_CASE("M18-D2d2 generic spring runtime is finite at 192 kHz 4x") {
    auto model = Model();
    model.mode_count = 16U;
    model.base_frequency_hz = 100.0;
    model.mode_spacing_hz = 200.0;
    model.dispersion = 1.0;
    model.decay_seconds = 3.0;
    model.high_frequency_damping = 10.0;
    model.maximum_output = 1.0;
    solo_rig::GenericSpringRuntime runtime;
    REQUIRE(runtime.Prepare(model, 192000.0, 4U) == solo_rig::GenericSpringError::None);
    double output{};
    for (int index = 0; index < 4096; ++index) {
        const double input = index % 2 == 0 ? 1.0e300 : -1.0e300;
        REQUIRE(runtime.Process(input, output) == solo_rig::GenericSpringError::None);
        REQUIRE(std::isfinite(output));
        REQUIRE(output >= -1.0 && output <= 1.0);
        REQUIRE(std::isfinite(runtime.CurrentStateEnergy()));
    }
    REQUIRE(runtime.Process(std::numeric_limits<double>::infinity(), output) ==
            solo_rig::GenericSpringError::NonFinite);
    REQUIRE(output == 0.0);
    static_assert(noexcept(std::declval<solo_rig::GenericSpringRuntime&>().Process(
        0.0, std::declval<double&>())));
}

TEST_CASE("M18-D2d2 generic spring runtime rejects unsafe preparation") {
    solo_rig::GenericSpringRuntime runtime;
    double output{123.0};
    REQUIRE(runtime.Process(0.0, output) == solo_rig::GenericSpringError::NotPrepared);
    REQUIRE(output == 0.0);
    REQUIRE(runtime.Prepare(Model(), 0.0, 1U) ==
            solo_rig::GenericSpringError::InvalidSampleRate);
    REQUIRE(runtime.Prepare(Model(), 100.0, 0U) ==
            solo_rig::GenericSpringError::InvalidOversampling);
    REQUIRE(runtime.Prepare(Model(), 100.0, 17U) ==
            solo_rig::GenericSpringError::InvalidOversampling);
    REQUIRE(runtime.SetSampleRate(100.0) == solo_rig::GenericSpringError::NotPrepared);
}
