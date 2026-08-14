#include "dsp/nonlinear_transformer_core.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>

TEST_CASE("M18-C8b generic core saturates with an analytic differential") {
    const solo_rig::NonlinearTransformerCoreModel model{
        .magnetizing_inductance = 1.0, .saturation_flux = 0.5,
        .hysteresis_width = 0.05, .hysteresis_rate = 1000.0,
        .remanent_flux = 0.0, .max_flux = 0.8, .max_current = 4.0, .max_step = 2.0};
    REQUIRE(solo_rig::ValidateNonlinearTransformerCoreModel(model));
    const auto low = solo_rig::EvaluateNonlinearTransformerCore(model, 0.1, 0.0);
    const auto high = solo_rig::EvaluateNonlinearTransformerCore(model, 4.0, 0.0);
    REQUIRE(low.Succeeded());
    REQUIRE(high.Succeeded());
    REQUIRE(std::fabs(low.flux - 0.5 * std::tanh(0.2)) < 1.0e-14);
    REQUIRE(low.differential_flux > high.differential_flux);
    REQUIRE(std::fabs(high.flux) < model.max_flux);
}

TEST_CASE("M18-C8b generic core hysteresis state and remanence reset deterministically") {
    const solo_rig::NonlinearTransformerCoreModel model{
        .magnetizing_inductance = 1.0, .saturation_flux = 1.0,
        .hysteresis_width = 0.2, .hysteresis_rate = 100.0,
        .remanent_flux = 0.1, .max_flux = 1.5, .max_current = 10.0, .max_step = 1.0};
    solo_rig::NonlinearTransformerCoreState state{.flux = model.remanent_flux,
                                                  .hysteresis = model.remanent_flux};
    REQUIRE(solo_rig::AdvanceNonlinearTransformerCoreState(model, 2.0, 1.0 / 48000.0, state));
    REQUIRE(state.hysteresis > model.remanent_flux);
    REQUIRE(state.flux > 0.0);
    const auto first = state;
    REQUIRE(solo_rig::AdvanceNonlinearTransformerCoreState(model, -2.0, 1.0 / 48000.0, state));
    REQUIRE(state.hysteresis < first.hysteresis);
    state = {.flux = model.remanent_flux, .hysteresis = model.remanent_flux};
    REQUIRE(solo_rig::AdvanceNonlinearTransformerCoreState(model, 2.0, 1.0 / 48000.0, state));
    REQUIRE(std::fabs(state.flux - first.flux) < 1.0e-15);
    REQUIRE(std::fabs(state.hysteresis - first.hysteresis) < 1.0e-15);
}

TEST_CASE("M18-C8b generic core rejects unsafe and nonfinite states") {
    auto model = solo_rig::NonlinearTransformerCoreModel{};
    model.max_flux = 0.5;
    REQUIRE(!solo_rig::ValidateNonlinearTransformerCoreModel(model));
    model.max_flux = 2.0;
    REQUIRE(solo_rig::ValidateNonlinearTransformerCoreModel(model));
    model.remanent_flux = 3.0;
    REQUIRE(!solo_rig::ValidateNonlinearTransformerCoreModel(model));
    model.remanent_flux = 0.0;
    REQUIRE(!solo_rig::EvaluateNonlinearTransformerCore(model,
        std::numeric_limits<double>::quiet_NaN(), 0.0).Succeeded());
    solo_rig::NonlinearTransformerCoreState state{};
    state.flux = std::numeric_limits<double>::infinity();
    REQUIRE(!solo_rig::AdvanceNonlinearTransformerCoreState(model, 0.0, 1.0 / 48000.0, state));
}

TEST_CASE("M18-C8b magnetizing inductance controls small-signal slope") {
    auto low_inductance = solo_rig::NonlinearTransformerCoreModel{
        .magnetizing_inductance = 0.5, .saturation_flux = 1.0,
        .hysteresis_width = 0.0, .hysteresis_rate = 100.0,
        .remanent_flux = 0.0, .max_flux = 2.0, .max_current = 10.0, .max_step = 1.0};
    auto high_inductance = low_inductance;
    high_inductance.magnetizing_inductance = 2.0;
    const auto low = solo_rig::EvaluateNonlinearTransformerCore(low_inductance, 1.0e-5, 0.0);
    const auto high = solo_rig::EvaluateNonlinearTransformerCore(high_inductance, 1.0e-5, 0.0);
    REQUIRE(low.Succeeded());
    REQUIRE(high.Succeeded());
    REQUIRE(std::fabs(low.differential_flux - 0.5) < 1.0e-8);
    REQUIRE(std::fabs(high.differential_flux - 2.0) < 1.0e-8);
    REQUIRE(high.flux > low.flux);
}
