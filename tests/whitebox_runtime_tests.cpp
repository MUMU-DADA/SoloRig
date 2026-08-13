#include "content/whitebox_runtime.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace {

std::string ReadFixture(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" / "whitebox" / name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open M13 fixture: " + path.string());
    }
    std::ostringstream source;
    source << stream.rdbuf();
    return source.str();
}

solo_rig::WhiteboxCompiledGraph Compile(std::string_view fixture) {
    const auto result = solo_rig::CompileWhiteboxGraphV2Json(ReadFixture(fixture));
    if (!result.graph.has_value()) {
        throw std::runtime_error("M13 fixture did not compile");
    }
    return *result.graph;
}

} // namespace

TEST_CASE("M13 generic MNA runtime solves a divider and resets deterministically") {
    const auto graph = Compile("m13_linear_divider.json");
    solo_rig::WhiteboxRuntimeOptions options;
    const auto prepared = solo_rig::PrepareWhiteboxRuntime(graph, options);
    REQUIRE(prepared.error == std::nullopt);
    REQUIRE(prepared.runtime.has_value());
    auto runtime = std::move(*prepared.runtime);
    double first{};
    REQUIRE(runtime.SolveDc(1.0, first) == solo_rig::WhiteboxRuntimeError::None);
    REQUIRE(std::fabs(first - 0.5) < 1.0e-9);
    double second{};
    REQUIRE(runtime.Process(0.25, second) == solo_rig::WhiteboxRuntimeError::None);
    runtime.Reset();
    double after_reset{};
    REQUIRE(runtime.SolveDc(1.0, after_reset) == solo_rig::WhiteboxRuntimeError::None);
    REQUIRE(std::fabs(after_reset - first) < 1.0e-12);
}

TEST_CASE("M13 capacitor state, oversampling and option errors are explicit") {
    const auto graph = Compile("m13_linear_dynamic.json");
    solo_rig::WhiteboxRuntimeOptions options;
    options.oversampling = 4;
    const auto prepared = solo_rig::PrepareWhiteboxRuntime(graph, options);
    REQUIRE(prepared.error == std::nullopt);
    REQUIRE(prepared.runtime.has_value());
    auto runtime = std::move(*prepared.runtime);
    double output{};
    REQUIRE(runtime.Process(1.0, output) == solo_rig::WhiteboxRuntimeError::None);
    REQUIRE(std::isfinite(output));
    const auto invalid = [&] {
        auto bad = options;
        bad.oversampling = 17;
        return solo_rig::PrepareWhiteboxRuntime(graph, bad);
    }();
    REQUIRE(invalid.error == solo_rig::WhiteboxRuntimeError::InvalidOptions);
}

TEST_CASE("M13 WDF planning exposes adaptors but runtime rejects unimplemented backend") {
    const auto graph = Compile("m13_wdf_plan.json");
    const auto plan = solo_rig::BuildWhiteboxWdfPlan(graph);
    REQUIRE(plan.has_value());
    REQUIRE(plan->topology == solo_rig::WhiteboxTopologyClass::Series);
    REQUIRE(plan->adaptors.size() == 2U);
    REQUIRE(plan->adaptors[0] == solo_rig::WhiteboxWdfAdaptorKind::Series);
    REQUIRE(plan->adaptors[1] == solo_rig::WhiteboxWdfAdaptorKind::RType);
    solo_rig::WhiteboxRuntimeOptions options;
    options.backend = solo_rig::WhiteboxRuntimeBackend::Wdf;
    const auto prepared = solo_rig::PrepareWhiteboxRuntime(graph, options);
    REQUIRE(prepared.error == solo_rig::WhiteboxRuntimeError::UnsupportedBackend);

    const auto parallel = Compile("m12_parallel.json");
    const auto parallel_plan = solo_rig::BuildWhiteboxWdfPlan(parallel);
    REQUIRE(parallel_plan.has_value());
    REQUIRE(parallel_plan->topology == solo_rig::WhiteboxTopologyClass::Parallel);
    REQUIRE(parallel_plan->adaptors.size() == 2U);
    REQUIRE(parallel_plan->adaptors[0] == solo_rig::WhiteboxWdfAdaptorKind::Parallel);
    REQUIRE(parallel_plan->adaptors[1] == solo_rig::WhiteboxWdfAdaptorKind::RType);
}

TEST_CASE("M13 nonlinear group has bounded Newton and stable nonconvergence") {
    const auto graph = Compile("m13_nonlinear_group.json");
    solo_rig::WhiteboxRuntimeOptions options;
    options.max_iterations = 32;
    auto prepared = solo_rig::PrepareWhiteboxRuntime(graph, options);
    REQUIRE(prepared.error == std::nullopt);
    REQUIRE(prepared.runtime.has_value());
    auto runtime = std::move(*prepared.runtime);
    double output{};
    REQUIRE(runtime.Process(1.0, output) == solo_rig::WhiteboxRuntimeError::None);
    REQUIRE(std::isfinite(output));
    options.max_iterations = 1;
    prepared = solo_rig::PrepareWhiteboxRuntime(graph, options);
    REQUIRE(prepared.runtime.has_value());
    runtime = std::move(*prepared.runtime);
    REQUIRE(runtime.Process(1.0, output) == solo_rig::WhiteboxRuntimeError::NonConvergent);
}

TEST_CASE("M13 runtime reports singular, invalid initial, unsupported and nonfinite states") {
    auto singular = PrepareWhiteboxRuntime(Compile("m13_singular.json"), {});
    REQUIRE(singular.runtime.has_value());
    double output{};
    REQUIRE(singular.runtime->Process(1.0, output) == solo_rig::WhiteboxRuntimeError::SingularMatrix);

    const auto invalid_initial = PrepareWhiteboxRuntime(Compile("m13_invalid_initial.json"), {});
    REQUIRE(invalid_initial.error == solo_rig::WhiteboxRuntimeError::InvalidInitialState);

    auto nonfinite = PrepareWhiteboxRuntime(Compile("m13_linear_divider.json"), {});
    REQUIRE(nonfinite.runtime.has_value());
    REQUIRE(nonfinite.runtime->Process(std::numeric_limits<double>::quiet_NaN(), output) ==
            solo_rig::WhiteboxRuntimeError::NonFinite);

    const auto unsupported = PrepareWhiteboxRuntime(Compile("m13_unsupported_component.json"), {});
    REQUIRE(unsupported.error == solo_rig::WhiteboxRuntimeError::UnsupportedComponent);

    auto invalid_backend_options = solo_rig::WhiteboxRuntimeOptions{};
    invalid_backend_options.backend = static_cast<solo_rig::WhiteboxRuntimeBackend>(99);
    const auto invalid_backend = PrepareWhiteboxRuntime(Compile("m13_linear_divider.json"),
                                                        invalid_backend_options);
    REQUIRE(invalid_backend.error == solo_rig::WhiteboxRuntimeError::InvalidBackend);
}

TEST_CASE("M13 Process is a noexcept prepared real-time boundary") {
    static_assert(noexcept(std::declval<solo_rig::WhiteboxRuntime&>().Process(0.0, std::declval<double&>())));
    static_assert(noexcept(std::declval<solo_rig::WhiteboxRuntime&>().SolveDc(0.0, std::declval<double&>())));
    solo_rig::WhiteboxRuntime runtime;
    double output{};
    REQUIRE(runtime.Process(0.0, output) == solo_rig::WhiteboxRuntimeError::NotPrepared);
}
