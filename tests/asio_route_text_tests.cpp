#include "standalone/asio_route_text.hpp"
#include "test_framework.hpp"

TEST_CASE("Standalone ASIO route text parses multiple devices and channel maps") {
    const auto parsed = solo_rig::standalone::ParseAsioRouteText(
        L"0:in=2,3;out=4,5 | 1:in=0;out=1", 2U);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->size() == 2U);
    REQUIRE(parsed->at(0).driver_index == 0U);
    REQUIRE(parsed->at(0).inputs.at(0) == 2U);
    REQUIRE(parsed->at(0).outputs.at(1) == 5U);
    REQUIRE(parsed->at(1).driver_index == 1U);
    REQUIRE(parsed->at(1).inputs.size() == 1U);
}

TEST_CASE("Standalone ASIO route text rejects duplicate devices incomplete fields and bad indices") {
    REQUIRE(!solo_rig::standalone::ParseAsioRouteText(L"0:in=0;out=0|0:in=1;out=1", 2U).has_value());
    REQUIRE(!solo_rig::standalone::ParseAsioRouteText(L"0:in=0", 1U).has_value());
    REQUIRE(!solo_rig::standalone::ParseAsioRouteText(L"1:in=0;out=0", 1U).has_value());
    REQUIRE(!solo_rig::standalone::ParseAsioRouteText(L"0:in=0,;out=0", 1U).has_value());
}
