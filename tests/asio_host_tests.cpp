#include "standalone/asio_host.hpp"
#include "test_framework.hpp"

#include <set>

TEST_CASE("ASIO driver discovery exposes stable identifiers without opening a stream") {
    const auto drivers = solo_rig::standalone::AsioHost::EnumerateDrivers();
    std::set<std::string> identifiers;
    for (const auto& driver : drivers) {
        REQUIRE(!driver.driver_id.empty());
        REQUIRE(!driver.display_name.empty());
        REQUIRE(!driver.class_id_text.empty());
        REQUIRE(identifiers.insert(driver.driver_id).second);
    }
}

TEST_CASE("ASIO host lifecycle is safe before a device is selected") {
    solo_rig::standalone::AsioHost host;
    REQUIRE(!host.IsOpen());
    REQUIRE(!host.IsRunning());
    host.Close();
    host.Stop();
    REQUIRE(!host.IsOpen());
    REQUIRE(!host.IsRunning());
}
