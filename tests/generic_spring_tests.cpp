#include "dsp/generic_spring.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>

namespace {

solo_rig::GenericSpringModel Model() {
    return {.mode_count = 3U,
        .base_frequency_hz = 100.0,
        .mode_spacing_hz = 50.0,
        .dispersion = 1.0,
        .decay_seconds = 1.0,
        .high_frequency_damping = 3.0,
        .drive_gain = 2.0,
        .pickup_gain = 3.0,
        .wet = 0.8,
        .dry = 0.2,
        .maximum_input = 2.0,
        .maximum_output = 10.0};
}

} // namespace

TEST_CASE("M18-D2d2 generic spring mode coefficients match dispersion and damping contract") {
    solo_rig::GenericSpringMode first{};
    solo_rig::GenericSpringMode middle{};
    solo_rig::GenericSpringMode last{};
    REQUIRE(solo_rig::EvaluateGenericSpringMode(Model(), 0U, 1000.0, first) ==
            solo_rig::GenericSpringError::None);
    REQUIRE(solo_rig::EvaluateGenericSpringMode(Model(), 1U, 1000.0, middle) ==
            solo_rig::GenericSpringError::None);
    REQUIRE(solo_rig::EvaluateGenericSpringMode(Model(), 2U, 1000.0, last) ==
            solo_rig::GenericSpringError::None);

    REQUIRE(first.frequency_hz == 100.0);
    REQUIRE(middle.frequency_hz == 175.0);
    REQUIRE(last.frequency_hz == 300.0);
    REQUIRE(std::fabs(first.decay_seconds - 1.0) < 1.0e-12);
    REQUIRE(std::fabs(middle.decay_seconds - (1.0 / 1.75)) < 1.0e-12);
    REQUIRE(std::fabs(last.decay_seconds - 0.25) < 1.0e-12);
    REQUIRE(std::fabs(first.pole_radius - std::exp(-0.001)) < 1.0e-12);
    REQUIRE(std::fabs(first.input_coefficient - (1.0 - first.pole_radius)) < 1.0e-15);
    REQUIRE(std::fabs(first.cosine - std::cos(0.2 * 3.14159265358979323846)) < 1.0e-12);
    REQUIRE(std::fabs(first.sine - std::sin(0.2 * 3.14159265358979323846)) < 1.0e-12);
}

TEST_CASE("M18-D2d2 generic spring enforces mode index and strict frequency margin") {
    solo_rig::GenericSpringMode mode{};
    REQUIRE(solo_rig::EvaluateGenericSpringMode(Model(), 3U, 1000.0, mode) ==
            solo_rig::GenericSpringError::InvalidMode);

    auto boundary = Model();
    boundary.mode_count = 1U;
    boundary.base_frequency_hz = 45.0;
    boundary.mode_spacing_hz = 0.0;
    REQUIRE(solo_rig::EvaluateGenericSpringMode(boundary, 0U, 100.0, mode) ==
            solo_rig::GenericSpringError::InvalidSampleRate);
    boundary.base_frequency_hz = 44.0;
    REQUIRE(solo_rig::EvaluateGenericSpringMode(boundary, 0U, 100.0, mode) ==
            solo_rig::GenericSpringError::None);
}

TEST_CASE("M18-D2d2 generic spring validates fixed capacity and finite model ranges") {
    auto invalid = Model();
    invalid.mode_count = 0U;
    REQUIRE(!solo_rig::ValidateGenericSpringModel(invalid));
    invalid = Model();
    invalid.mode_count = 17U;
    REQUIRE(!solo_rig::ValidateGenericSpringModel(invalid));
    invalid = Model();
    invalid.decay_seconds = 0.0;
    REQUIRE(!solo_rig::ValidateGenericSpringModel(invalid));
    invalid = Model();
    invalid.wet = 1.1;
    REQUIRE(!solo_rig::ValidateGenericSpringModel(invalid));
    invalid = Model();
    invalid.base_frequency_hz = std::numeric_limits<double>::infinity();
    REQUIRE(!solo_rig::ValidateGenericSpringModel(invalid));

    solo_rig::GenericSpringMode mode{1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
    REQUIRE(solo_rig::EvaluateGenericSpringMode(
        Model(), 0U, std::numeric_limits<double>::quiet_NaN(), mode) ==
        solo_rig::GenericSpringError::InvalidSampleRate);
    REQUIRE(mode.frequency_hz == 0.0);
}

TEST_CASE("M18-D2d2 generic spring supports the maximum finite modal contract") {
    auto extreme = Model();
    extreme.mode_count = 16U;
    extreme.base_frequency_hz = 1.0;
    extreme.mode_spacing_hz = 1.0;
    extreme.dispersion = 4.0;
    extreme.decay_seconds = 30.0;
    extreme.high_frequency_damping = 100.0;
    extreme.drive_gain = 1.0e6;
    extreme.pickup_gain = 1.0e6;
    extreme.maximum_input = 1.0e6;
    extreme.maximum_output = 1.0e6;
    for (std::uint32_t index = 0U; index < extreme.mode_count; ++index) {
        solo_rig::GenericSpringMode mode{};
        REQUIRE(solo_rig::EvaluateGenericSpringMode(extreme, index, 768000.0, mode) ==
                solo_rig::GenericSpringError::None);
        REQUIRE(std::isfinite(mode.pole_radius));
        REQUIRE(mode.pole_radius > 0.0 && mode.pole_radius < 1.0);
    }
}
