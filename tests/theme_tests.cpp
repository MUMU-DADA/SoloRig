#include "content/theme.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <string>
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

TEST_CASE("Theme parser enforces bounded collections and external file size") {
    std::string many_colors = R"({
        "schemaVersion":1,
        "id":"bounded",
        "displayName":"Bounded",
        "colors":{)";
    for (std::size_t index = 0U; index < 129U; ++index) {
        if (index != 0U) many_colors += ",";
        many_colors += "\"color" + std::to_string(index) + "\":\"#102030\"";
    }
    many_colors += R"(},"metrics":{"gridSize":16}})";
    REQUIRE(std::get<solo_rig::ThemeLoadError>(solo_rig::ParseTheme(many_colors)) ==
            solo_rig::ThemeLoadError::ResourceLimit);

    const auto oversized = std::filesystem::temp_directory_path() / "solorig-theme-oversized.json";
    std::error_code cleanup;
    std::filesystem::remove(oversized, cleanup);
    {
        std::ofstream output(oversized, std::ios::binary | std::ios::trunc);
        output << std::string(4U * 1024U * 1024U + 1U, 'x');
    }
    const auto loaded = solo_rig::LoadThemeFile(oversized);
    REQUIRE(std::get<solo_rig::ThemeLoadError>(loaded) == solo_rig::ThemeLoadError::ResourceLimit);
    std::filesystem::remove(oversized, cleanup);
}

TEST_CASE("SoloRig ships coherent built-in dark and light themes") {
    REQUIRE(solo_rig::DefaultDarkTheme().id == "solorig.dark");
    REQUIRE(solo_rig::DefaultLightTheme().id == "solorig.light");
    REQUIRE(solo_rig::DefaultDarkTheme().colors.contains("accent"));
    REQUIRE(solo_rig::DefaultLightTheme().metrics.at("gridSize") > 0.0);
}
