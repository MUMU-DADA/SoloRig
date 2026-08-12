#include "content/module_package.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path CreateTestPackage(std::string_view manifest) {
    const auto directory = std::filesystem::temp_directory_path() / "SoloRigModulePackageTest";
    std::filesystem::create_directories(directory);
    std::ofstream output(directory / "manifest.json", std::ios::binary | std::ios::trunc);
    output << manifest;
    return directory;
}

void RemoveTestPackage(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::remove(directory / "manifest.json", error);
    std::filesystem::remove(directory, error);
}

} // namespace

TEST_CASE("LoadModulePackage loads metadata and optional custom pedal appearance") {
    const auto directory = CreateTestPackage(R"({
        "schemaVersion": 1,
        "moduleId": "org.mumu-dada.solorig.drive",
        "displayName": "Reference Drive",
        "category": "analog-circuit",
        "logicFile": "logic/circuit.json",
        "skinFile": "ui/pedal.json",
        "declaredLatencySamples": 0,
        "ports": [
            {"id":"input","direction":"input","maxChannels":2},
            {"id":"output","direction":"output","maxChannels":2}
        ],
        "parameters": [{"id":"drive","minimum":0,"maximum":1,"default":0.5}]
    })");

    const auto result = solo_rig::LoadModulePackage(directory);
    RemoveTestPackage(directory);

    REQUIRE(result.errors.empty());
    REQUIRE(result.package.has_value());
    REQUIRE(result.package->descriptor.module_id == "org.mumu-dada.solorig.drive");
    REQUIRE(result.package->custom_skin_path.has_value());
}

TEST_CASE("LoadModulePackage rejects asset paths that escape the package directory") {
    const auto directory = CreateTestPackage(R"({
        "schemaVersion": 1,
        "moduleId": "org.mumu-dada.solorig.unsafe",
        "displayName": "Unsafe",
        "category": "digital-algorithm",
        "logicFile": "../outside.json",
        "ports": [
            {"id":"input","direction":"input","maxChannels":1},
            {"id":"output","direction":"output","maxChannels":1}
        ]
    })");

    const auto result = solo_rig::LoadModulePackage(directory);
    RemoveTestPackage(directory);

    REQUIRE(!result.package.has_value());
    REQUIRE(!result.errors.empty());
    REQUIRE(result.errors.front() == solo_rig::ModulePackageLoadError::UnsafeAssetPath);
}

TEST_CASE("IsSafeRelativeAssetPath rejects absolute and traversal paths") {
    REQUIRE(solo_rig::IsSafeRelativeAssetPath("logic/algorithm.json"));
    REQUIRE(!solo_rig::IsSafeRelativeAssetPath("../logic.json"));
    REQUIRE(!solo_rig::IsSafeRelativeAssetPath("C:/logic.json"));
}
