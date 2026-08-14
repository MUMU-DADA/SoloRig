#include "dsp/dynamic_speaker_load.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>

namespace {

solo_rig::DynamicSpeakerLoadModel Model() {
    return {.voice_coil_resistance = 6.0, .voice_coil_inductance = 5.0e-4,
            .force_factor = 5.0, .moving_mass = 0.02, .mechanical_resistance = 2.0,
            .compliance = 5.0e-4, .nonlinear_stiffness = 1000.0,
            .nonlinear_displacement_scale = 0.002, .maximum_current = 10.0,
            .maximum_velocity = 100.0, .maximum_displacement = 0.1};
}

} // namespace

TEST_CASE("M18-C10b speaker evaluator satisfies mechanical BE residual and back EMF") {
    const auto model = Model();
    constexpr double dt = 1.0e-4;
    constexpr double current = 0.5;
    const solo_rig::DynamicSpeakerLoadState previous{.current = 0.1, .velocity = 0.02,
                                                      .displacement = 0.001};
    const auto evaluated = solo_rig::EvaluateDynamicSpeakerLoad(model, current, previous, dt);
    REQUIRE(evaluated.Succeeded());
    const double x = evaluated.next_state.displacement;
    const double u = evaluated.next_state.velocity;
    const double effective_current = model.maximum_current * std::tanh(current / model.maximum_current);
    const double residual = model.moving_mass * (u - previous.velocity) / dt +
        model.mechanical_resistance * u + x / model.compliance +
        model.nonlinear_stiffness * model.nonlinear_displacement_scale *
            std::tanh(x / model.nonlinear_displacement_scale) -
        model.force_factor * effective_current;
    REQUIRE(std::fabs(residual) < 1.0e-10);
    const double electrical = model.voice_coil_resistance * effective_current +
        model.voice_coil_inductance * (effective_current - previous.current) / dt +
        model.force_factor * u;
    REQUIRE(std::fabs(evaluated.voltage - electrical) < 1.0e-12);
}

TEST_CASE("M18-C10b speaker analytic branch derivative matches centered finite difference") {
    const auto model = Model();
    const solo_rig::DynamicSpeakerLoadState previous{.current = 0.1, .velocity = -0.01,
                                                      .displacement = 0.0005};
    constexpr double current = 0.4;
    constexpr double step = 1.0e-6;
    constexpr double dt = 1.0 / 48000.0;
    const auto center = solo_rig::EvaluateDynamicSpeakerLoad(model, current, previous, dt);
    const auto plus = solo_rig::EvaluateDynamicSpeakerLoad(model, current + step, previous, dt);
    const auto minus = solo_rig::EvaluateDynamicSpeakerLoad(model, current - step, previous, dt);
    REQUIRE(center.Succeeded());
    REQUIRE(plus.Succeeded());
    REQUIRE(minus.Succeeded());
    const double finite_difference = (plus.voltage - minus.voltage) / (2.0 * step);
    REQUIRE(std::fabs(center.voltage_derivative_current - finite_difference) < 1.0e-6);
}

TEST_CASE("M18-C10b speaker suspension amplitude and bounded branches are explicit") {
    auto linear = Model();
    linear.nonlinear_stiffness = 0.0;
    const auto small_linear = solo_rig::EvaluateDynamicSpeakerLoad(linear, 0.01, {}, 0.001);
    const auto small_nonlinear = solo_rig::EvaluateDynamicSpeakerLoad(Model(), 0.01, {}, 0.001);
    const auto large_linear = solo_rig::EvaluateDynamicSpeakerLoad(linear, 5.0, {}, 0.001);
    const auto large_nonlinear = solo_rig::EvaluateDynamicSpeakerLoad(Model(), 5.0, {}, 0.001);
    REQUIRE(small_linear.Succeeded());
    REQUIRE(small_nonlinear.Succeeded());
    REQUIRE(large_linear.Succeeded());
    REQUIRE(large_nonlinear.Succeeded());
    REQUIRE(std::fabs(large_linear.voltage - large_nonlinear.voltage) >
            std::fabs(small_linear.voltage - small_nonlinear.voltage));

    const auto extreme = solo_rig::EvaluateDynamicSpeakerLoad(Model(), 1.0e308, {}, 1.0);
    REQUIRE(extreme.Succeeded());
    REQUIRE(std::isfinite(extreme.voltage));
    REQUIRE(extreme.voltage_derivative_current == 0.0);
}

TEST_CASE("M18-C10b speaker exposes DC coil resistance and a mechanical resonance peak") {
    auto dc_model = Model();
    dc_model.voice_coil_inductance = 0.0;
    dc_model.force_factor = 0.0;
    const auto dc = solo_rig::EvaluateDynamicSpeakerLoad(dc_model, 0.0, {}, 0.001);
    REQUIRE(dc.Succeeded());
    REQUIRE(dc.voltage == 0.0);
    REQUIRE(std::fabs(dc.voltage_derivative_current - dc_model.voice_coil_resistance) < 1.0e-12);

    auto response_model = Model();
    response_model.nonlinear_stiffness = 0.0;
    response_model.maximum_current = 1000.0;
    const auto impedance_rms = [&response_model](double frequency) {
        constexpr double sample_rate = 48000.0;
        constexpr double amplitude = 0.01;
        solo_rig::DynamicSpeakerLoadState state{};
        double squared_voltage{};
        std::uint32_t count{};
        for (std::uint32_t sample = 0; sample < 48000U; ++sample) {
            const double phase = 6.2831853071795864769 * frequency *
                static_cast<double>(sample) / sample_rate;
            const auto evaluated = solo_rig::EvaluateDynamicSpeakerLoad(
                response_model, amplitude * std::sin(phase), state, 1.0 / sample_rate);
            REQUIRE(evaluated.Succeeded());
            state = evaluated.next_state;
            if (sample >= 24000U) {
                squared_voltage += evaluated.voltage * evaluated.voltage;
                ++count;
            }
        }
        return std::sqrt(squared_voltage / static_cast<double>(count));
    };
    const double low = impedance_rms(5.0);
    const double resonance = impedance_rms(50.0);
    const double high = impedance_rms(500.0);
    REQUIRE(resonance > low * 1.2);
    REQUIRE(resonance > high * 1.2);
}

TEST_CASE("M18-C10b speaker rejects unsafe models states and nonfinite runtime") {
    auto invalid = Model();
    invalid.moving_mass = 0.0;
    REQUIRE(!solo_rig::ValidateDynamicSpeakerLoadModel(invalid));
    REQUIRE(solo_rig::EvaluateDynamicSpeakerLoad(
        Model(), std::numeric_limits<double>::infinity(), {}, 0.001).error ==
        solo_rig::NonlinearDeviceError::NonFinite);
    REQUIRE(solo_rig::EvaluateDynamicSpeakerLoad(
        Model(), 0.0, {.current = 11.0}, 0.001).error ==
        solo_rig::NonlinearDeviceError::InvalidModel);
}
