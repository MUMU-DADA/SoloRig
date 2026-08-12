#include "ui/pedal_layout.hpp"
#include "test_framework.hpp"

TEST_CASE("PedalLayout stores module locations and moves pedals on a snapped graph canvas") {
    solo_rig::PedalLayout layout(16.0);
    REQUIRE(layout.Add("drive-1", {.x = 1.0, .y = 2.0}));
    REQUIRE(layout.Move("drive-1", {.x = 23.0, .y = 39.0}, true));

    const auto position = layout.Find("drive-1");
    REQUIRE(position.has_value());
    REQUIRE(position->x == 16.0);
    REQUIRE(position->y == 32.0);
}

TEST_CASE("PedalLayout refuses duplicate, missing and non-finite module positions") {
    solo_rig::PedalLayout layout;
    REQUIRE(layout.Add("delay-1", {.x = 0.0, .y = 0.0}));
    REQUIRE(!layout.Add("delay-1", {.x = 0.0, .y = 0.0}));
    REQUIRE(!layout.Move("missing", {.x = 0.0, .y = 0.0}, false));
}
