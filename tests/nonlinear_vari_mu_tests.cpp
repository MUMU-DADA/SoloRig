#include "dsp/nonlinear_vari_mu.hpp"
#include "test_framework.hpp"

#include <array>
#include <cmath>
#include <limits>

TEST_CASE("M18-C7c vari-mu evaluator covers remote cutoff KCL and full Jacobian") {
    const solo_rig::NonlinearVariMuModel model{.amplification_factor = 20.0,
                                               .perveance = 0.001,
                                               .remote_cutoff_coefficient = 2.0,
                                               .max_current = 1.0,
                                               .max_step = 0.5};
    const auto zero_bias = solo_rig::EvaluateNonlinearVariMu(model, 5.0, 0.0, 0.0);
    REQUIRE(zero_bias.Succeeded());
    REQUIRE(std::fabs(zero_bias.currents[0] - 0.000125) < 1.0e-15);
    const auto remote_cutoff = solo_rig::EvaluateNonlinearVariMu(model, 30.0, -1.0, 0.0);
    REQUIRE(remote_cutoff.Succeeded());
    REQUIRE(remote_cutoff.currents[0] > 0.0);
    REQUIRE(remote_cutoff.currents[0] < 0.001 * std::pow(0.5, 1.5));

    for (const auto point : std::array<std::array<double, 3>, 3>{
             {{{5.0, 0.1, 0.0}}, {{30.0, -1.0, 0.0}}, {{10.0, -0.2, 0.1}}}}) {
        const auto evaluated = solo_rig::EvaluateNonlinearVariMu(model, point[0], point[1], point[2]);
        REQUIRE(evaluated.Succeeded());
        REQUIRE(std::fabs(evaluated.currents[0] + evaluated.currents[1] + evaluated.currents[2]) < 1.0e-15);
        for (std::size_t column = 0; column < 3U; ++column) {
            auto plus = point;
            auto minus = point;
            plus[column] += 1.0e-7;
            minus[column] -= 1.0e-7;
            const auto plus_eval = solo_rig::EvaluateNonlinearVariMu(model, plus[0], plus[1], plus[2]);
            const auto minus_eval = solo_rig::EvaluateNonlinearVariMu(model, minus[0], minus[1], minus[2]);
            for (std::size_t row = 0; row < 3U; ++row) {
                const double finite_difference =
                    (plus_eval.currents[row] - minus_eval.currents[row]) / 2.0e-7;
                REQUIRE(std::fabs(finite_difference - evaluated.jacobian[row][column]) < 1.0e-7);
            }
        }
    }
    REQUIRE(solo_rig::EvaluateNonlinearVariMu(model, -1.0, 0.0, 0.0).currents[0] == 0.0);
    REQUIRE(solo_rig::EvaluateNonlinearVariMu(model, 5.0, -0.25, 0.0).currents[0] == 0.0);
}

TEST_CASE("M18-C7c vari-mu clamp returns a matching zero Jacobian") {
    auto model = solo_rig::NonlinearVariMuModel{.amplification_factor = 20.0,
                                                .perveance = 0.001,
                                                .remote_cutoff_coefficient = 2.0,
                                                .max_current = 1.0e-5,
                                                .max_step = 0.5};
    const auto clamped = solo_rig::EvaluateNonlinearVariMu(model, 30.0, -1.0, 0.0);
    REQUIRE(clamped.Succeeded());
    REQUIRE(clamped.currents[0] == model.max_current);
    REQUIRE(clamped.currents[2] == -model.max_current);
    for (const auto& row : clamped.jacobian) {
        for (const auto derivative : row) REQUIRE(derivative == 0.0);
    }
}

TEST_CASE("M18-C7c vari-mu evaluator handles extreme finite and rejects non-finite data") {
    auto model = solo_rig::NonlinearVariMuModel{.amplification_factor = 20.0,
                                                .perveance = 0.001,
                                                .remote_cutoff_coefficient = 2.0,
                                                .max_current = 0.01,
                                                .max_step = 0.5};
    for (const auto point : std::array<std::array<double, 3>, 3>{
             {{{std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
                -std::numeric_limits<double>::max()}},
              {{std::numeric_limits<double>::max(), 0.0, -std::numeric_limits<double>::max()}},
              {{0.0, -std::numeric_limits<double>::max(), std::numeric_limits<double>::max()}}}}) {
        const auto evaluated = solo_rig::EvaluateNonlinearVariMu(model, point[0], point[1], point[2]);
        REQUIRE(evaluated.Succeeded());
        for (const auto current : evaluated.currents) REQUIRE(std::isfinite(current));
        for (const auto& row : evaluated.jacobian) {
            for (const auto derivative : row) REQUIRE(std::isfinite(derivative));
        }
    }
    REQUIRE(solo_rig::EvaluateNonlinearVariMu(
                model, 1.0, std::numeric_limits<double>::infinity(), 0.0).error ==
            solo_rig::NonlinearDeviceError::NonFinite);
    model.remote_cutoff_coefficient = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(!solo_rig::ValidateNonlinearVariMuModel(model));
}
