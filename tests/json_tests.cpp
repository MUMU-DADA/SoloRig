#include "content/json.hpp"
#include "test_framework.hpp"

TEST_CASE("ParseJson reads nested manifest-style objects and arrays") {
    const auto parsed = solo_rig::ParseJson(R"({"name":"Drive","ports":[{"id":"input","maxChannels":2}]})");

    REQUIRE(parsed.value.has_value());
    const auto* object = parsed.value->AsObject();
    REQUIRE(object != nullptr);
    REQUIRE(object->at("name").AsString() != nullptr);
    REQUIRE(*object->at("name").AsString() == "Drive");
    REQUIRE(object->at("ports").AsArray() != nullptr);
}

TEST_CASE("ParseJson rejects duplicate keys and malformed tokens") {
    REQUIRE(!solo_rig::ParseJson("{\"id\":1,\"id\":2}").value.has_value());
    REQUIRE(!solo_rig::ParseJson("{\"id\":}").value.has_value());
    REQUIRE(!solo_rig::ParseJson("[1,]").value.has_value());
}

TEST_CASE("EscapeJsonString escapes persisted string delimiters") {
    REQUIRE(solo_rig::EscapeJsonString("a\"b\\c\n") == "a\\\"b\\\\c\\n");
}
