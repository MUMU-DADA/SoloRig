#include "dsp/nonlinear_amplifier.hpp"
#include "test_framework.hpp"

#include <array>
#include <cmath>
#include <limits>

TEST_CASE("M18-C5 bounded amplifier evaluator matches transfer Jacobian and clamp semantics") {
    const solo_rig::NonlinearAmplifierModel opamp{.kind = solo_rig::AmplifierKind::OpAmp,
                                                   .gain = 100.0,
                                                   .minimum_output = -2.0,
                                                   .maximum_output = 2.0,
                                                   .max_step = 0.5};
    for (const auto point : std::array<std::array<double, 2>, 3>{
             {{{0.01, 0.0}}, {{0.0, 0.01}}, {{0.25, 0.25}}}}) {
        const auto evaluated = solo_rig::EvaluateNonlinearAmplifier(opamp, point[0], point[1]);
        REQUIRE(evaluated.Succeeded());
        for (std::size_t column = 0; column < 2U; ++column) {
            auto plus = point;
            auto minus = point;
            plus[column] += 1.0e-7;
            minus[column] -= 1.0e-7;
            const auto plus_value = solo_rig::EvaluateNonlinearAmplifier(opamp, plus[0], plus[1]);
            const auto minus_value = solo_rig::EvaluateNonlinearAmplifier(opamp, minus[0], minus[1]);
            REQUIRE(std::fabs((plus_value.output - minus_value.output) / 2.0e-7 -
                              evaluated.jacobian[column]) < 1.0e-7);
        }
    }
    for (const double differential : {-1.0, 1.0}) {
        const auto clamped = solo_rig::EvaluateNonlinearAmplifier(opamp, differential, 0.0);
        REQUIRE(clamped.Succeeded());
        REQUIRE(clamped.output == (differential < 0.0 ? -2.0 : 2.0));
        REQUIRE(clamped.jacobian[0] == 0.0);
        REQUIRE(clamped.jacobian[1] == 0.0);
    }
}

TEST_CASE("M18-C5 amplifier models reject invalid data and clamp extreme finite input before overflow") {
    auto ota = solo_rig::NonlinearAmplifierModel{.kind = solo_rig::AmplifierKind::Ota,
                                                  .gain = 1.0e6,
                                                  .minimum_output = -0.002,
                                                  .maximum_output = 0.002,
                                                  .max_step = 0.5};
    REQUIRE(solo_rig::ValidateNonlinearAmplifierModel(ota));
    for (const double input : {-1.0e300, 1.0e300}) {
        const auto evaluated = solo_rig::EvaluateNonlinearAmplifier(ota, input, 0.0);
        REQUIRE(evaluated.Succeeded());
        REQUIRE(std::isfinite(evaluated.output));
        REQUIRE(std::fabs(evaluated.output) == 0.002);
        REQUIRE(evaluated.jacobian[0] == 0.0);
    }
    REQUIRE(solo_rig::EvaluateNonlinearAmplifier(
                ota, std::numeric_limits<double>::quiet_NaN(), 0.0).error ==
            solo_rig::NonlinearDeviceError::NonFinite);
    ota.maximum_output = ota.minimum_output;
    REQUIRE(!solo_rig::ValidateNonlinearAmplifierModel(ota));
    ota.maximum_output = 0.002;
    ota.gain = std::numeric_limits<double>::infinity();
    REQUIRE(!solo_rig::ValidateNonlinearAmplifierModel(ota));
}
