#include "content/circuit_v2.hpp"
#include "test_framework.hpp"

#include <string>

namespace {

std::string Netlist(std::string bindings, std::string components = R"(
    {"id":"pot","type":"potentiometer","pins":["in","0","out"],"value":10000,
     "potentiometer":{"position":0.25,"minimum":0,"maximum":1}},
    {"id":"sw","type":"switch","pins":["out","0"],
     "switch":{"states":["open","closed"],"initial":0,"openResistance":1000000,"closedResistance":10}})" ) {
    return R"({"version":2,"schema":"circuit","referenceGround":"0","nodes":["in","out","vcc","0"],
      "ports":[{"id":"input","direction":"input","node":"in"},{"id":"output","direction":"output","node":"out"}],
      "supplies":[{"id":"rail","positive":"vcc","negative":"0","voltage":9}],
      "components":[)" + components + R"(],"parameterBindings":)" + bindings +
      R"(,"solver":{"method":"modified-nodal-analysis","integration":"backward-euler","oversampling":4}})";
}

} // namespace

TEST_CASE("M18-D1a parser accepts versioned potentiometer switch and supply bindings") {
    const auto parsed = solo_rig::ParseCircuitV2Json(Netlist(R"([
      {"modelVersion":1,"parameter":"position","target":"pot","property":"potentiometer.position","smoothingSeconds":0.01},
      {"modelVersion":1,"parameter":"engaged","target":"sw","property":"switch.state","smoothingSeconds":0},
      {"modelVersion":1,"parameter":"voltage","target":"rail","property":"supply.voltage","smoothingSeconds":0.02}])"));
    REQUIRE(parsed.circuit.has_value());
    REQUIRE(parsed.circuit->parameter_bindings.size() == 3U);
    REQUIRE(parsed.circuit->components.at(1).switch_spec->open_resistance == 1000000.0);
    REQUIRE(parsed.circuit->components.at(1).switch_spec->closed_resistance == 10.0);
}

TEST_CASE("M18-D1a parser rejects duplicate unknown mismatched and nonfinite bindings") {
    const auto duplicate = solo_rig::ParseCircuitV2Json(Netlist(R"([
      {"modelVersion":1,"parameter":"p","target":"pot","property":"potentiometer.position","smoothingSeconds":0},
      {"modelVersion":1,"parameter":"p","target":"rail","property":"supply.voltage","smoothingSeconds":0}])"));
    REQUIRE(!duplicate.circuit.has_value());
    REQUIRE(duplicate.errors.front() == solo_rig::CircuitLoadError::DuplicateId);

    const auto unknown = solo_rig::ParseCircuitV2Json(Netlist(R"([
      {"modelVersion":1,"parameter":"p","target":"missing","property":"potentiometer.position","smoothingSeconds":0}])"));
    REQUIRE(!unknown.circuit.has_value());

    const auto mismatch = solo_rig::ParseCircuitV2Json(Netlist(R"([
      {"modelVersion":1,"parameter":"p","target":"rail","property":"potentiometer.position","smoothingSeconds":0}])"));
    REQUIRE(!mismatch.circuit.has_value());

    const auto nonfinite = solo_rig::ParseCircuitV2Json(Netlist(R"([
      {"modelVersion":1,"parameter":"p","target":"pot","property":"potentiometer.position","smoothingSeconds":1e309}])"));
    REQUIRE(!nonfinite.circuit.has_value());
}

TEST_CASE("M18-D1a switch contract requires two named states and finite positive endpoint resistances") {
    const auto bad = solo_rig::ParseCircuitV2Json(Netlist(R"([
      {"modelVersion":1,"parameter":"engaged","target":"sw","property":"switch.state","smoothingSeconds":0}])", R"(
      {"id":"sw","type":"switch","pins":["out","0"],
       "switch":{"states":["closed","open"],"initial":0,"openResistance":0,"closedResistance":10}})"));
    REQUIRE(!bad.circuit.has_value());
    REQUIRE(bad.errors.front() == solo_rig::CircuitLoadError::InvalidValue);
}
