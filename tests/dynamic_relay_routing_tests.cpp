#include "dsp/dynamic_relay_routing.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>

namespace {

solo_rig::DynamicRelayModel CoilModel(double bounce = 0.0) {
    return {.coil_resistance = 100.0, .coil_inductance = 0.01,
        .open_resistance = 1.0e6, .closed_resistance = 10.0,
        .pull_in_current = 0.02, .drop_out_current = 0.01,
        .operate_time_seconds = 0.001, .release_time_seconds = 0.002,
        .bounce_duration_seconds = bounce, .bounce_frequency_hz = bounce > 0.0 ? 1000.0 : 0.0,
        .bounce_amplitude = bounce > 0.0 ? 0.2 : 0.0, .maximum_coil_current = 1.0};
}

} // namespace

TEST_CASE("M18-D1b3 routing maps released and operated positions to finite NC and NO paths") {
    const solo_rig::DynamicRelayRoutingModel routing{.pole_count = 2U, .contact_gap = 0.2};
    const auto released = solo_rig::EvaluateDynamicRelayRouting(CoilModel(), routing, {});
    REQUIRE(released.Succeeded());
    REQUIRE(released.effective_position == 0.0);
    REQUIRE(released.normally_closed_resistance == CoilModel().closed_resistance);
    REQUIRE(released.normally_open_resistance == CoilModel().open_resistance);

    const auto operated = solo_rig::EvaluateDynamicRelayRouting(
        CoilModel(), routing, {.position = 1.0, .energized = true});
    REQUIRE(operated.Succeeded());
    REQUIRE(operated.normally_closed_resistance == CoilModel().open_resistance);
    REQUIRE(operated.normally_open_resistance == CoilModel().closed_resistance);
}

TEST_CASE("M18-D1b3 routing enforces break before make inside the contact gap") {
    const solo_rig::DynamicRelayRoutingModel routing{.pole_count = 4U, .contact_gap = 0.2};
    const auto lower = solo_rig::EvaluateDynamicRelayRouting(
        CoilModel(), routing, {.position = 0.4});
    const auto middle = solo_rig::EvaluateDynamicRelayRouting(
        CoilModel(), routing, {.position = 0.5});
    const auto upper = solo_rig::EvaluateDynamicRelayRouting(
        CoilModel(), routing, {.position = 0.6});
    REQUIRE(lower.Succeeded());
    REQUIRE(middle.Succeeded());
    REQUIRE(upper.Succeeded());
    REQUIRE(lower.normally_closed_resistance == CoilModel().open_resistance);
    REQUIRE(lower.normally_open_resistance == CoilModel().open_resistance);
    REQUIRE(middle.normally_closed_resistance == CoilModel().open_resistance);
    REQUIRE(middle.normally_open_resistance == CoilModel().open_resistance);
    REQUIRE(upper.normally_closed_resistance == CoilModel().open_resistance);
    REQUIRE(upper.normally_open_resistance == CoilModel().open_resistance);

    const auto moving = solo_rig::EvaluateDynamicRelayRouting(
        CoilModel(), routing, {.position = 0.2});
    REQUIRE(moving.normally_closed_resistance > CoilModel().closed_resistance);
    REQUIRE(moving.normally_closed_resistance < CoilModel().open_resistance);
    REQUIRE(moving.normally_open_resistance == CoilModel().open_resistance);
}

TEST_CASE("M18-D1b3 routing shares one bounded bounce position across all poles") {
    const auto model = CoilModel(0.003);
    const solo_rig::DynamicRelayRoutingModel routing{.pole_count = 3U, .contact_gap = 0.1};
    const solo_rig::DynamicRelayState state{.position = 0.8,
        .bounce_remaining_seconds = 0.002, .bounce_phase = 1.0, .energized = true};
    const auto first = solo_rig::EvaluateDynamicRelayRouting(model, routing, state);
    const auto repeated = solo_rig::EvaluateDynamicRelayRouting(model, routing, state);
    REQUIRE(first.Succeeded());
    REQUIRE(first.effective_position >= 0.0);
    REQUIRE(first.effective_position <= 1.0);
    REQUIRE(first.normally_closed_resistance == repeated.normally_closed_resistance);
    REQUIRE(first.normally_open_resistance == repeated.normally_open_resistance);
}

TEST_CASE("M18-D1b3 routing rejects unsafe pole gap and nonfinite states") {
    auto routing = solo_rig::DynamicRelayRoutingModel{.pole_count = 0U, .contact_gap = 0.1};
    REQUIRE(!solo_rig::ValidateDynamicRelayRoutingModel(routing));
    REQUIRE(solo_rig::EvaluateDynamicRelayRouting(CoilModel(), routing, {}).error ==
            solo_rig::DynamicRelayRoutingError::InvalidModel);
    routing = {.pole_count = 5U, .contact_gap = 0.1};
    REQUIRE(!solo_rig::ValidateDynamicRelayRoutingModel(routing));
    routing = {.pole_count = 1U, .contact_gap = 0.91};
    REQUIRE(!solo_rig::ValidateDynamicRelayRoutingModel(routing));
    routing = {.pole_count = 1U, .contact_gap = 0.1};
    REQUIRE(solo_rig::EvaluateDynamicRelayRouting(CoilModel(), routing,
        {.position = std::numeric_limits<double>::quiet_NaN()}).error ==
        solo_rig::DynamicRelayRoutingError::NonFinite);
}
