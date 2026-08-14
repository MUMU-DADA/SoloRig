#include "dsp/nonlinear_diode.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>

TEST_CASE("M18-C1 diode IV and analytic Jacobian match the clean-room formula") {
    const solo_rig::NonlinearDeviceModel model{
        .kind = solo_rig::NonlinearDeviceKind::Diode,
        .saturation_current = 1.0e-12,
        .thermal_voltage = 0.02585,
        .max_step = 0.5};
    const auto zero = solo_rig::EvaluateNonlinearDevice(model, 0.0);
    REQUIRE(zero.Succeeded());
    REQUIRE(std::fabs(zero.current) < 1.0e-24);
    REQUIRE(std::fabs(zero.jacobian - model.saturation_current / model.thermal_voltage) < 1.0e-20);

    constexpr double voltage = 0.5;
    const auto point = solo_rig::EvaluateNonlinearDevice(model, voltage);
    const auto exponential = std::exp(voltage / model.thermal_voltage);
    REQUIRE(point.Succeeded());
    REQUIRE(std::fabs(point.current - model.saturation_current * (exponential - 1.0)) < 1.0e-12);
    REQUIRE(std::fabs(point.jacobian - model.saturation_current / model.thermal_voltage * exponential) < 1.0e-12);
}

TEST_CASE("M18-C1 Zener IV covers forward zero-bias and reverse breakdown") {
    const solo_rig::NonlinearDeviceModel model{
        .kind = solo_rig::NonlinearDeviceKind::Zener,
        .saturation_current = 1.0e-12,
        .thermal_voltage = 0.02585,
        .breakdown_voltage = 5.1,
        .breakdown_current = 1.0e-6,
        .breakdown_slope = 0.1,
        .max_step = 1.0};
    const auto zero = solo_rig::EvaluateNonlinearDevice(model, 0.0);
    const auto forward = solo_rig::EvaluateNonlinearDevice(model, 0.5);
    const auto reverse = solo_rig::EvaluateNonlinearDevice(model, -5.2);
    REQUIRE(zero.Succeeded());
    REQUIRE(std::fabs(zero.current) < 1.0e-24);
    REQUIRE(forward.Succeeded());
    REQUIRE(forward.current > 0.0);
    REQUIRE(reverse.Succeeded());
    REQUIRE(reverse.current < -1.0e-6);
    REQUIRE(reverse.jacobian > 0.0);
}

TEST_CASE("M18-C1 nonlinear models reject nonfinite and unsafe parameters") {
    auto model = solo_rig::NonlinearDeviceModel{};
    REQUIRE(!solo_rig::ValidateNonlinearDeviceModel(model));
    model.kind = solo_rig::NonlinearDeviceKind::Diode;
    model.saturation_current = 1.0e-12;
    model.thermal_voltage = 0.02585;
    model.max_step = 0.5;
    REQUIRE(solo_rig::ValidateNonlinearDeviceModel(model));
    REQUIRE(solo_rig::EvaluateNonlinearDevice(model, std::numeric_limits<double>::quiet_NaN()).error ==
            solo_rig::NonlinearDeviceError::NonFinite);
    model.thermal_voltage = std::numeric_limits<double>::infinity();
    REQUIRE(!solo_rig::ValidateNonlinearDeviceModel(model));
}

TEST_CASE("M18-C1 nonlinear evaluation keeps extreme finite inputs bounded") {
    const solo_rig::NonlinearDeviceModel model{
        .kind = solo_rig::NonlinearDeviceKind::Zener,
        .saturation_current = 1.0e-12,
        .thermal_voltage = 0.02585,
        .breakdown_voltage = 5.1,
        .breakdown_current = 1.0e-6,
        .breakdown_slope = 0.1,
        .max_step = 10.0};
    for (const double voltage : {-1.0e12, 1.0e12}) {
        const auto point = solo_rig::EvaluateNonlinearDevice(model, voltage);
        REQUIRE(point.Succeeded());
        REQUIRE(std::isfinite(point.current));
        REQUIRE(std::isfinite(point.jacobian));
    }
}
