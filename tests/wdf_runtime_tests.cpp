#include "content/wdf_runtime.hpp"
#include "content/whitebox_compiler.hpp"
#include "content/whitebox_runtime.hpp"
#include "test_framework.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {

std::string ReadFixture(std::string_view name) {
    const auto path = std::filesystem::path(SOLO_RIG_SOURCE_DIRECTORY) / "fixtures" / "whitebox" / name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open M18-A fixture: " + path.string());
    }
    std::ostringstream source;
    source << stream.rdbuf();
    return source.str();
}

solo_rig::WhiteboxCompiledGraph CompileGraph(std::string_view source) {
    const auto compiled = solo_rig::CompileWhiteboxGraphV2Json(source);
    if (!compiled.graph.has_value()) {
        throw std::runtime_error("cannot compile M18-A graph");
    }
    return *compiled.graph;
}

solo_rig::WhiteboxRuntime PrepareRuntime(const solo_rig::WhiteboxCompiledGraph& graph,
                                         solo_rig::WhiteboxRuntimeBackend backend,
                                         double sample_rate_hz = 48000.0) {
    solo_rig::WhiteboxRuntimeOptions options;
    options.backend = backend;
    options.sample_rate_hz = sample_rate_hz;
    auto prepared = solo_rig::PrepareWhiteboxRuntime(graph, options);
    if (!prepared.runtime.has_value()) {
        throw std::runtime_error("cannot prepare M18-A runtime");
    }
    return std::move(*prepared.runtime);
}

} // namespace

TEST_CASE("M18-A WDF one-port exposes waves, state, reset and sample-rate updates") {
    const auto prepared = solo_rig::PrepareWdfOnePort(
        solo_rig::WdfElementKind::Capacitor, 1.0e-6, 48000.0, 0.25);
    REQUIRE(prepared.error == std::nullopt);
    REQUIRE(prepared.port.has_value());
    auto port = std::move(*prepared.port);
    const auto first_resistance = port.PortResistance();
    double reflected{};
    REQUIRE(port.Process(0.5, reflected) == solo_rig::WdfError::None);
    REQUIRE(std::fabs(reflected - 0.25) < 1.0e-12);
    REQUIRE(std::fabs(port.Port().Voltage() - 0.375) < 1.0e-12);
    REQUIRE(std::fabs(port.Port().Current() - 0.125 / first_resistance) < 1.0e-12);
    REQUIRE(port.SetSampleRate(96000.0));
    REQUIRE(port.PortResistance() < first_resistance);
    const auto updated_resistance = port.PortResistance();
    REQUIRE(!port.SetSampleRate(std::numeric_limits<double>::max()));
    REQUIRE(port.PortResistance() == updated_resistance);
    port.Reset();
    REQUIRE(port.Process(0.5, reflected) == solo_rig::WdfError::None);
    REQUIRE(std::fabs(reflected - 0.25) < 1.0e-12);

    const auto inductor_prepared = solo_rig::PrepareWdfOnePort(
        solo_rig::WdfElementKind::Inductor, 1.0, 1.0, 0.25);
    REQUIRE(inductor_prepared.port.has_value());
    auto inductor = std::move(*inductor_prepared.port);
    REQUIRE(inductor.Process(0.75, reflected) == solo_rig::WdfError::None);
    REQUIRE(std::fabs(reflected + 0.25) < 1.0e-12);
    REQUIRE(std::fabs(inductor.Port().Voltage() - 0.25) < 1.0e-12);
    REQUIRE(std::fabs(inductor.Port().Current() - 0.5) < 1.0e-12);
}

TEST_CASE("M18-A Series and Parallel adaptors perform deterministic scattering") {
    const std::array<double, 2> resistances{1000.0, 1000.0};
    const auto series = solo_rig::PrepareWdfAdaptor(solo_rig::WdfAdaptorKind::Series, resistances);
    const auto parallel = solo_rig::PrepareWdfAdaptor(solo_rig::WdfAdaptorKind::Parallel, resistances);
    REQUIRE(series.error == std::nullopt && parallel.error == std::nullopt);
    REQUIRE(series.adaptor.has_value() && parallel.adaptor.has_value());
    const std::array<double, 2> incident{1.0, 0.0};
    std::array<double, 2> series_reflected{};
    std::array<double, 2> parallel_reflected{};
    REQUIRE(series.adaptor->Scatter(incident, series_reflected) == solo_rig::WdfError::None);
    REQUIRE(parallel.adaptor->Scatter(incident, parallel_reflected) == solo_rig::WdfError::None);
    REQUIRE(std::fabs(series_reflected[0]) < 1.0e-12);
    REQUIRE(std::fabs(series_reflected[1] + 1.0) < 1.0e-12);
    REQUIRE(std::fabs(parallel_reflected[0]) < 1.0e-12);
    REQUIRE(std::fabs(parallel_reflected[1] - 1.0) < 1.0e-12);
}

TEST_CASE("M18-A R-type adaptor provides transformer scattering without allocation") {
    const std::array<double, 2> resistances{50.0, 200.0};
    const auto prepared = solo_rig::PrepareWdfAdaptor(solo_rig::WdfAdaptorKind::RType, resistances, 2.0);
    REQUIRE(prepared.error == std::nullopt);
    REQUIRE(prepared.adaptor.has_value());
    const std::array<double, 2> incident{1.0, 0.0};
    std::array<double, 2> reflected{};
    REQUIRE(prepared.adaptor->Scatter(incident, reflected) == solo_rig::WdfError::None);
    REQUIRE(std::isfinite(reflected[0]) && std::isfinite(reflected[1]));
    REQUIRE(std::fabs(reflected[0] - (750.0 / 850.0)) < 1.0e-12);
    REQUIRE(std::fabs(reflected[1] - (800.0 / 850.0)) < 1.0e-12);
    const double incident_power = incident[0] * incident[0] / (4.0 * resistances[0]);
    const double reflected_power = reflected[0] * reflected[0] / (4.0 * resistances[0]) +
        reflected[1] * reflected[1] / (4.0 * resistances[1]);
    REQUIRE(std::fabs(incident_power - reflected_power) < 1.0e-12);
}

TEST_CASE("M18-A WDF runtime is selected from circuit IR and agrees with the linear boundary") {
    const auto compiled = solo_rig::CompileWhiteboxGraphV2Json(ReadFixture("m18a_wdf_series.json"));
    REQUIRE(compiled.error == std::nullopt);
    REQUIRE(compiled.graph.has_value());
    solo_rig::WhiteboxRuntimeOptions options;
    options.backend = solo_rig::WhiteboxRuntimeBackend::Wdf;
    options.oversampling = 2;
    const auto unsupported_oversampling = solo_rig::PrepareWhiteboxRuntime(*compiled.graph, options);
    REQUIRE(unsupported_oversampling.error == solo_rig::WhiteboxRuntimeError::InvalidOptions);
    options.oversampling = 1;
    auto prepared = solo_rig::PrepareWhiteboxRuntime(*compiled.graph, options);
    REQUIRE(prepared.error == std::nullopt);
    REQUIRE(prepared.runtime.has_value());
    auto runtime = std::move(*prepared.runtime);
    double first{};
    double second{};
    REQUIRE(runtime.Process(0.5, first) == solo_rig::WhiteboxRuntimeError::None);
    REQUIRE(runtime.Process(0.5, second) == solo_rig::WhiteboxRuntimeError::None);
    REQUIRE(std::isfinite(first) && std::isfinite(second));
    REQUIRE(first == second);
    runtime.Reset();
    double after_reset{};
    REQUIRE(runtime.Process(0.5, after_reset) == solo_rig::WhiteboxRuntimeError::None);
    REQUIRE(after_reset == first);
}

TEST_CASE("M18-A WDF matches MNA for a resistive divider") {
    const auto graph = CompileGraph(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"series","kind":"resistor","pins":["in","out"],"value":1000},
                    {"id":"load","kind":"resistor","pins":["out","0"],"value":1000}],
      "subcircuits":[],"modelReferences":[],"backend":{"preferred":"mna"}
    })");
    auto mna = PrepareRuntime(graph, solo_rig::WhiteboxRuntimeBackend::Mna);
    auto wdf = PrepareRuntime(graph, solo_rig::WhiteboxRuntimeBackend::Wdf);
    for (const double input : {-1.0, -0.25, 0.0, 0.5, 1.0}) {
        double mna_output{};
        double wdf_output{};
        REQUIRE(mna.Process(input, mna_output) == solo_rig::WhiteboxRuntimeError::None);
        REQUIRE(wdf.Process(input, wdf_output) == solo_rig::WhiteboxRuntimeError::None);
        REQUIRE(std::fabs(wdf_output - mna_output) < 1.0e-12);
    }
}

TEST_CASE("M18-A parallel WDF graph executes a floating passive network") {
    const auto graph = CompileGraph(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"branch-a","kind":"resistor","pins":["in","out"],"value":1000},
                    {"id":"branch-b","kind":"resistor","pins":["out","in"],"value":2200}],
      "subcircuits":[],"modelReferences":[],"backend":{"preferred":"wdf"}
    })");
    auto runtime = PrepareRuntime(graph, solo_rig::WhiteboxRuntimeBackend::Wdf);
    for (const double input : {-0.5, 0.0, 0.75}) {
        double output{};
        REQUIRE(runtime.Process(input, output) == solo_rig::WhiteboxRuntimeError::None);
        REQUIRE(std::fabs(output - input) < 1.0e-12);
    }
    runtime.Reset();
    double output{};
    REQUIRE(runtime.Process(0.25, output) == solo_rig::WhiteboxRuntimeError::None);
    REQUIRE(std::fabs(output - 0.25) < 1.0e-12);
}

TEST_CASE("M18-A WDF RC and RLC networks match backward-Euler MNA state") {
    const auto compare = [](std::string_view source) {
        const auto graph = CompileGraph(source);
        auto mna = PrepareRuntime(graph, solo_rig::WhiteboxRuntimeBackend::Mna);
        auto wdf = PrepareRuntime(graph, solo_rig::WhiteboxRuntimeBackend::Wdf);
        std::array<double, 8> first_pass{};
        for (std::size_t index = 0; index < first_pass.size(); ++index) {
            const double input = index < 5U ? 1.0 : -0.25;
            double mna_output{};
            double wdf_output{};
            REQUIRE(mna.Process(input, mna_output) == solo_rig::WhiteboxRuntimeError::None);
            REQUIRE(wdf.Process(input, wdf_output) == solo_rig::WhiteboxRuntimeError::None);
            REQUIRE(std::fabs(wdf_output - mna_output) < 1.0e-10);
            first_pass[index] = wdf_output;
        }
        wdf.Reset();
        for (std::size_t index = 0; index < first_pass.size(); ++index) {
            const double input = index < 5U ? 1.0 : -0.25;
            double output{};
            REQUIRE(wdf.Process(input, output) == solo_rig::WhiteboxRuntimeError::None);
            REQUIRE(std::fabs(output - first_pass[index]) < 1.0e-12);
        }
        REQUIRE(mna.SetSampleRate(96000.0));
        REQUIRE(wdf.SetSampleRate(96000.0));
        mna.Reset();
        wdf.Reset();
        double mna_output{};
        double wdf_output{};
        REQUIRE(mna.Process(1.0, mna_output) == solo_rig::WhiteboxRuntimeError::None);
        REQUIRE(wdf.Process(1.0, wdf_output) == solo_rig::WhiteboxRuntimeError::None);
        REQUIRE(std::fabs(wdf_output - mna_output) < 1.0e-10);
    };

    compare(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"series","kind":"resistor","pins":["in","out"],"value":1000},
                    {"id":"shunt","kind":"capacitor","pins":["out","0"],"value":1e-6,"initial":0}],
      "subcircuits":[],"modelReferences":[],"backend":{"preferred":"mna"}
    })");
    compare(R"({
      "version":2,"schema":"circuit","referenceGround":"0","nodes":["in","mid","out","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
      "components":[{"id":"series-r","kind":"resistor","pins":["in","mid"],"value":1000},
                    {"id":"series-l","kind":"inductor","pins":["mid","out"],"value":0.01,"initial":0},
                    {"id":"shunt-c","kind":"capacitor","pins":["out","0"],"value":1e-6,"initial":0}],
      "subcircuits":[],"modelReferences":[],"backend":{"preferred":"mna"}
    })");
}

TEST_CASE("M18-A WDF rejects nonfinite and invalid adaptor contracts deterministically") {
    const auto bad_port = solo_rig::PrepareWdfOnePort(solo_rig::WdfElementKind::Resistor, 0.0, 48000.0);
    REQUIRE(bad_port.error == solo_rig::WdfError::InvalidValue);
    const auto bad_adaptor = solo_rig::PrepareWdfAdaptor(solo_rig::WdfAdaptorKind::RType,
                                                         std::array<double, 3>{1.0, 2.0, 3.0});
    REQUIRE(bad_adaptor.error == solo_rig::WdfError::InvalidTopology);
    auto prepared = solo_rig::PrepareWdfOnePort(solo_rig::WdfElementKind::Resistor, 1000.0, 48000.0);
    REQUIRE(prepared.port.has_value());
    double output{};
    REQUIRE(prepared.port->Process(std::numeric_limits<double>::quiet_NaN(), output) == solo_rig::WdfError::NonFinite);
}

TEST_CASE("M18-A WDF scattering process is noexcept and uses caller-owned buffers") {
    static_assert(noexcept(std::declval<solo_rig::WdfOnePort&>().Process(0.0, std::declval<double&>())));
    static_assert(noexcept(std::declval<const solo_rig::WdfAdaptor&>().Scatter(
        std::span<const double>{}, std::span<double>{})));
    static_assert(noexcept(std::declval<solo_rig::WhiteboxRuntime&>().Process(
        0.0, std::declval<double&>())));
}
