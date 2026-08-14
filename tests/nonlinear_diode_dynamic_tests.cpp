#include "dsp/nonlinear_diode_dynamic.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>

namespace {

solo_rig::NonlinearDiodeDynamicModel Model() {
    return {.static_model = {.kind = solo_rig::NonlinearDeviceKind::Diode,
                             .saturation_current = 1.0e-12,
                             .thermal_voltage = 0.02585,
                             .max_step = 1.0},
            .junction_capacitance = 1.0e-9,
            .junction_potential = 0.7,
            .grading_coefficient = 0.5,
            .temperature_celsius = 25.0,
            .leakage_current = 1.0e-12,
            .leakage_temperature_coefficient = 0.01};
}

} // namespace

TEST_CASE("M18-C9a dynamic diode temperature and leakage alter IV with analytic Jacobian") {
    auto nominal = Model();
    auto hot = nominal;
    hot.temperature_celsius = 125.0;
    hot.leakage_current = 1.0e-9;
    const auto cold_point = solo_rig::EvaluateNonlinearDiodeDynamic(nominal, 0.2, 0.0, 48000.0);
    const auto hot_point = solo_rig::EvaluateNonlinearDiodeDynamic(hot, 0.2, 0.0, 48000.0);
    REQUIRE(cold_point.Succeeded());
    REQUIRE(hot_point.Succeeded());
    REQUIRE(std::fabs(cold_point.current - hot_point.current) > 1.0e-12);
    const double delta = 1.0e-7;
    const auto plus = solo_rig::EvaluateNonlinearDiodeDynamic(nominal, 0.2 + delta, 0.0, 48000.0);
    const auto minus = solo_rig::EvaluateNonlinearDiodeDynamic(nominal, 0.2 - delta, 0.0, 48000.0);
    REQUIRE(std::fabs((plus.current - minus.current) / (2.0 * delta) - cold_point.jacobian) < 1.0e-6);
}

TEST_CASE("M18-C9a junction capacitance uses backward-Euler history and reset-safe state") {
    auto model = Model();
    model.leakage_current = 0.0;
    const auto first = solo_rig::EvaluateNonlinearDiodeDynamic(model, 0.3, 0.1, 1000.0);
    const auto second = solo_rig::EvaluateNonlinearDiodeDynamic(model, 0.3, 0.3, 1000.0);
    REQUIRE(first.Succeeded());
    REQUIRE(second.Succeeded());
    REQUIRE(first.current > second.current);
    REQUIRE(first.capacitance > 0.0);
    REQUIRE(first.jacobian > second.jacobian);
}

TEST_CASE("M18-C9a dynamic diode rejects unsafe temperature, capacitance and nonfinite boundaries") {
    auto model = Model();
    model.temperature_celsius = -101.0;
    REQUIRE(!solo_rig::ValidateNonlinearDiodeDynamicModel(model));
    model = Model();
    model.junction_capacitance = std::numeric_limits<double>::infinity();
    REQUIRE(!solo_rig::ValidateNonlinearDiodeDynamicModel(model));
    model = Model();
    const auto nonfinite = solo_rig::EvaluateNonlinearDiodeDynamic(
        model, std::numeric_limits<double>::quiet_NaN(), 0.0, 48000.0);
    REQUIRE(nonfinite.error == solo_rig::NonlinearDeviceError::NonFinite);
    const auto extreme = solo_rig::EvaluateNonlinearDiodeDynamic(model, 1.0e12, -1.0e12, 192000.0 * 16.0);
    REQUIRE(extreme.Succeeded());
    REQUIRE(std::isfinite(extreme.current));
    REQUIRE(std::isfinite(extreme.jacobian));
}
