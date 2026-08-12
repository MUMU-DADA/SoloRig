#include "core/realtime_load_meter.hpp"
#include "test_framework.hpp"

using solo_rig::RealtimeLoadMeter;

TEST_CASE("RealtimeLoadMeter converts duration to the current audio-block load") {
    RealtimeLoadMeter meter;
    meter.Configure(48000.0, 48);
    meter.RecordProcessingNanoseconds(500000);

    REQUIRE(meter.LastLoadFraction() == 0.5);
    REQUIRE(meter.PeakLoadFraction() == 0.5);
}

TEST_CASE("RealtimeLoadMeter retains the highest measured block load") {
    RealtimeLoadMeter meter;
    meter.Configure(48000.0, 48);
    meter.RecordProcessingNanoseconds(250000);
    meter.RecordProcessingNanoseconds(1500000);

    REQUIRE(meter.LastLoadFraction() == 1.5);
    REQUIRE(meter.PeakLoadFraction() == 1.5);
}
