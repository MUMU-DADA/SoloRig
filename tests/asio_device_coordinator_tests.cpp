#include "standalone/asio_device_coordinator.hpp"
#include "test_framework.hpp"

TEST_CASE("Standalone multi-ASIO fixed ring preserves frames after an underflow") {
    REQUIRE(solo_rig::standalone::AsioDeviceCoordinator::RunBufferContractSelfTest());
}
