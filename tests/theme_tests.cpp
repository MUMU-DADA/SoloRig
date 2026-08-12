#include "content/theme.hpp"
#include "test_framework.hpp"

#include <variant>

TEST_CASE("Theme parser accepts a standard external dark or light theme structure") {
    const auto parsed = solo_rig::ParseTheme(R"({
        "schemaVersion":1,
        "id":"test.dark",
        "displayName":"Test Dark",
        "colors":{"background":"#102030","text":"#FFFFFF"},
        "metrics":{"gridSize":16,"cornerRadius":8}
    })");

    REQUIRE(std::holds_alternative<solo_rig::ThemeDefinition>(parsed));
    REQUIRE(std::get<solo_rig::ThemeDefinition>(parsed).colors.at("background") == "#102030");
}

TEST_CASE("Theme parser rejects invalid external color and metric definitions") {
    const auto invalid_color = solo_rig::ParseTheme(R"({
        "schemaVersion":1,"id":"bad","displayName":"Bad","colors":{"background":"blue"},"metrics":{}
    })");
    const auto invalid_metric = solo_rig::ParseTheme(R"({
        "schemaVersion":1,"id":"bad","displayName":"Bad","colors":{},"metrics":{"gridSize":-1}
    })");

    REQUIRE(std::holds_alternative<solo_rig::ThemeLoadError>(invalid_color));
    REQUIRE(std::holds_alternative<solo_rig::ThemeLoadError>(invalid_metric));
}

TEST_CASE("SoloRig ships coherent built-in dark and light themes") {
    REQUIRE(solo_rig::DefaultDarkTheme().id == "solorig.dark");
    REQUIRE(solo_rig::DefaultLightTheme().id == "solorig.light");
    REQUIRE(solo_rig::DefaultDarkTheme().colors.contains("accent"));
    REQUIRE(solo_rig::DefaultLightTheme().metrics.at("gridSize") > 0.0);
}
