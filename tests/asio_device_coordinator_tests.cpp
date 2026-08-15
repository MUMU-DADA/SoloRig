#include "standalone/asio_device_coordinator.hpp"
#include "test_framework.hpp"

TEST_CASE("Standalone multi-ASIO fixed ring preserves frames after an underflow") {
    REQUIRE(solo_rig::standalone::AsioDeviceCoordinator::RunBufferContractSelfTest());
}

TEST_CASE("Standalone multi-ASIO offline clock contract stays bounded at 48 kHz and 44.1 kHz") {
    REQUIRE(solo_rig::standalone::AsioDeviceCoordinator::RunAsyncClockContractSelfTest());
}
