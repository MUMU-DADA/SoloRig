#include "dsp/dynamic_relay.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <limits>

namespace {

solo_rig::DynamicRelayModel Model(double bounce=0.0) {
    return {.coil_resistance=100.0,.coil_inductance=0.01,.open_resistance=1.0e6,
        .closed_resistance=10.0,.pull_in_current=0.02,.drop_out_current=0.01,
        .operate_time_seconds=0.001,.release_time_seconds=0.002,
        .bounce_duration_seconds=bounce,.bounce_frequency_hz=bounce>0.0?1000.0:0.0,
        .bounce_amplitude=bounce>0.0?0.2:0.0,.maximum_coil_current=1.0};
}

} // namespace

TEST_CASE("M18-D1b1 relay coil backward-Euler voltage and derivative match reference") {
    const auto evaluated=solo_rig::EvaluateDynamicRelay(Model(),0.03,
        {.coil_current=0.01,.position=0.0},1.0e-4);
    REQUIRE(evaluated.Succeeded());
    REQUIRE(std::fabs(evaluated.coil_voltage-(100.0*0.03+0.01/1e-4*(0.03-0.01)))<1e-12);
    REQUIRE(std::fabs(evaluated.coil_voltage_derivative_current-200.0)<1e-12);
    const double epsilon=1e-7;
    const auto plus=solo_rig::EvaluateDynamicRelay(Model(),0.03+epsilon,{.coil_current=.01},1e-4);
    const auto minus=solo_rig::EvaluateDynamicRelay(Model(),0.03-epsilon,{.coil_current=.01},1e-4);
    REQUIRE(std::fabs((plus.coil_voltage-minus.coil_voltage)/(2*epsilon)-
        evaluated.coil_voltage_derivative_current)<1e-6);
}

TEST_CASE("M18-D1b1 relay Schmitt state operate and release histories are explicit") {
    auto state=solo_rig::DynamicRelayState{};
    auto pull=solo_rig::EvaluateDynamicRelay(Model(),0.03,state,0.001);
    REQUIRE(pull.Succeeded()); REQUIRE(pull.next_state.energized); REQUIRE(pull.next_state.position==0.5);
    auto hold=solo_rig::EvaluateDynamicRelay(Model(),0.015,pull.next_state,0.001);
    REQUIRE(hold.next_state.energized); REQUIRE(hold.next_state.position>pull.next_state.position);
    auto drop=solo_rig::EvaluateDynamicRelay(Model(),0.005,hold.next_state,0.002);
    REQUIRE(!drop.next_state.energized); REQUIRE(drop.next_state.position<hold.next_state.position);
    REQUIRE(drop.contact_resistance>Model().closed_resistance);
    REQUIRE(drop.contact_resistance<Model().open_resistance);
}

TEST_CASE("M18-D1b1 relay bounce is deterministic bounded and decays") {
    const auto model=Model(0.003);
    auto first=solo_rig::EvaluateDynamicRelay(model,0.03,{},0.00025);
    auto second=solo_rig::EvaluateDynamicRelay(model,0.03,first.next_state,0.00025);
    auto repeated=solo_rig::EvaluateDynamicRelay(model,0.03,first.next_state,0.00025);
    REQUIRE(first.next_state.bounce_remaining_seconds>0.0);
    REQUIRE(second.contact_resistance==repeated.contact_resistance);
    REQUIRE(second.contact_resistance>=model.closed_resistance);
    REQUIRE(second.contact_resistance<=model.open_resistance);
    auto state=second.next_state;
    for(int index=0;index<20;++index) state=solo_rig::EvaluateDynamicRelay(model,0.03,state,0.00025).next_state;
    REQUIRE(state.bounce_remaining_seconds==0.0);
    REQUIRE(state.bounce_phase==0.0);
}

TEST_CASE("M18-D1b1 relay rejects unsafe models states and nonfinite runtime") {
    auto bad=Model(); bad.drop_out_current=bad.pull_in_current;
    REQUIRE(!solo_rig::ValidateDynamicRelayModel(bad));
    REQUIRE(solo_rig::EvaluateDynamicRelay(bad,0.0,{},1e-4).error==solo_rig::DynamicRelayError::InvalidModel);
    REQUIRE(solo_rig::EvaluateDynamicRelay(Model(),std::numeric_limits<double>::infinity(),{},1e-4).error==
        solo_rig::DynamicRelayError::NonFinite);
    REQUIRE(solo_rig::EvaluateDynamicRelay(Model(),2.0,{},1e-4).error==solo_rig::DynamicRelayError::NonFinite);
    bad=Model(); bad.open_resistance=bad.closed_resistance;
    REQUIRE(!solo_rig::ValidateDynamicRelayModel(bad));
}
