#include "content/whitebox_runtime.hpp"
#include "test_framework.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace {

solo_rig::WhiteboxCompiledGraph Compile(std::string_view source) {
    const auto result = solo_rig::CompileWhiteboxGraphV2Json(source);
    if (!result.graph.has_value()) {
        throw std::runtime_error("D1b2 runtime graph did not compile");
    }
    return *result.graph;
}

constexpr std::string_view kInline = R"({"version":2,"schema":"circuit","referenceGround":"0",
  "nodes":["in","out","0"],
  "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
  "supplies":[],"components":[{"id":"load","kind":"resistor","pins":["out","0"],"value":1000}],
  "subcircuits":[{"id":"stage","ports":["a","b","g"],"portMap":{"a":"in","b":"out","g":"0"},
    "inline":{"nodes":["a","x","b","g"],"components":[
      {"id":"r","kind":"resistor","pins":["a","x"],"value":470},
      {"id":"c","kind":"capacitor","pins":["x","g"],"value":0.000001,"initial":0.25},
      {"id":"l","kind":"inductor","pins":["x","b"],"value":0.01,"initial":0.005}]}}],
  "modelReferences":[],"backend":{"preferred":"mna"}})";

constexpr std::string_view kFlat = R"({"version":2,"schema":"circuit","referenceGround":"0",
  "nodes":["in","out","0","stage::x"],
  "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
  "supplies":[],"components":[
    {"id":"load","kind":"resistor","pins":["out","0"],"value":1000},
    {"id":"stage::r","kind":"resistor","pins":["in","stage::x"],"value":470},
    {"id":"stage::c","kind":"capacitor","pins":["stage::x","0"],"value":0.000001,"initial":0.25},
    {"id":"stage::l","kind":"inductor","pins":["stage::x","out"],"value":0.01,"initial":0.005}],
  "subcircuits":[],"modelReferences":[],"backend":{"preferred":"mna"}})";

} // namespace

TEST_CASE("M18-D1b2 inline RCL runtime matches manual flattening at 192 kHz 4x and Reset") {
    solo_rig::WhiteboxRuntimeOptions options;
    options.sample_rate_hz = 192000.0;
    options.oversampling = 4U;
    auto inline_prepared = solo_rig::PrepareWhiteboxRuntime(Compile(kInline), options);
    auto flat_prepared = solo_rig::PrepareWhiteboxRuntime(Compile(kFlat), options);
    REQUIRE(inline_prepared.runtime.has_value());
    REQUIRE(flat_prepared.runtime.has_value());
    const std::array<double, 8> inputs{0.0, 1.0, 1.0, -0.5, 0.25, 0.0, -1.0, 0.5};
    std::array<double, 8> first_pass{};
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        double inline_output{};
        double flat_output{};
        REQUIRE(inline_prepared.runtime->Process(inputs[index], inline_output) ==
                solo_rig::WhiteboxRuntimeError::None);
        REQUIRE(flat_prepared.runtime->Process(inputs[index], flat_output) ==
                solo_rig::WhiteboxRuntimeError::None);
        REQUIRE(std::fabs(inline_output - flat_output) < 1.0e-12);
        first_pass[index] = inline_output;
    }
    inline_prepared.runtime->Reset();
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        double output{};
        REQUIRE(inline_prepared.runtime->Process(inputs[index], output) ==
                solo_rig::WhiteboxRuntimeError::None);
        REQUIRE(std::fabs(output - first_pass[index]) < 1.0e-12);
    }
    double output{};
    REQUIRE(inline_prepared.runtime->Process(std::numeric_limits<double>::quiet_NaN(), output) ==
            solo_rig::WhiteboxRuntimeError::NonFinite);
    static_assert(noexcept(std::declval<solo_rig::WhiteboxRuntime&>().Process(0.0,
                                                                             std::declval<double&>())));
}

TEST_CASE("M18-D1b2 inline passive singular graph fails explicitly") {
    const auto source = R"({"version":2,"schema":"circuit","referenceGround":"0",
      "nodes":["in","out","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
      "supplies":[],"components":[],"subcircuits":[{"id":"floating","ports":["a","b"],
      "portMap":{"a":"in","b":"out"},"inline":{"nodes":["a","b","x","y"],"components":[
        {"id":"r","kind":"resistor","pins":["x","y"],"value":1000}]}}],
      "modelReferences":[],"backend":{"preferred":"mna"}})";
    auto prepared = solo_rig::PrepareWhiteboxRuntime(Compile(source), {});
    REQUIRE(prepared.runtime.has_value());
    double output{};
    REQUIRE(prepared.runtime->Process(1.0, output) == solo_rig::WhiteboxRuntimeError::SingularMatrix);
}
