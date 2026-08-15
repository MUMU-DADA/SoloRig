#include "dsp/generic_optocoupler_mna.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>

TEST_CASE("M20-B4 optical MNA evaluator advances illumination and LDR resistance") {
    solo_rig::GenericOptocouplerMnaModel model{};
    model.optical.attack_seconds = 0.001;
    model.optical.release_seconds = 0.01;
    model.emitter_resistance_ohms = 220.0;
    REQUIRE(solo_rig::ValidateGenericOptocouplerMnaModel(model));
    solo_rig::GenericOptocouplerMnaEvaluation dark{};
    REQUIRE(solo_rig::EvaluateGenericOptocouplerMna(model, 0.0, 0.0,
        1.0 / 192000.0, dark) == solo_rig::GenericOptocouplerError::None);
    solo_rig::GenericOptocouplerMnaEvaluation lit{};
    REQUIRE(solo_rig::EvaluateGenericOptocouplerMna(model, 5.0,
        dark.next_illumination, 1.0 / 192000.0, lit) ==
        solo_rig::GenericOptocouplerError::None);
    REQUIRE(lit.next_illumination > dark.next_illumination);
    REQUIRE(lit.ldr_resistance_ohms < dark.ldr_resistance_ohms);
}

TEST_CASE("M20-B4 optical MNA evaluator rejects unsafe model, state and timestep") {
    solo_rig::GenericOptocouplerMnaModel model{};
    solo_rig::GenericOptocouplerMnaEvaluation output{};
    model.emitter_resistance_ohms = 0.0;
    REQUIRE(solo_rig::EvaluateGenericOptocouplerMna(model, 1.0, 0.0,
        1.0 / 48000.0, output) == solo_rig::GenericOptocouplerError::InvalidModel);
    model.emitter_resistance_ohms = 100.0;
    REQUIRE(solo_rig::EvaluateGenericOptocouplerMna(model, 1.0,
        std::numeric_limits<double>::quiet_NaN(), 1.0 / 48000.0, output) ==
        solo_rig::GenericOptocouplerError::NonFinite);
    REQUIRE(solo_rig::EvaluateGenericOptocouplerMna(model, 1.0, 0.0, 0.0,
        output) == solo_rig::GenericOptocouplerError::InvalidSampleRate);
}
