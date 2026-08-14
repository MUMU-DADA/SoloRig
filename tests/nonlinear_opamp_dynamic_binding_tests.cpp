#include "content/nonlinear_opamp_binding.hpp"
#include "test_framework.hpp"

TEST_CASE("M18-C9e dynamic opamp audited binding preserves the generic POD contract") {
    solo_rig::ModelAuditRecord record;
    record.kind = solo_rig::ModelKind::Opamp;
    record.part_number = "self-authored-dynamic-opamp-no-commercial-part";
    record.parameters = {
        {"open-loop-gain",100,1,100000,"1"}, {"gain-bandwidth",1000,1,1000000,"Hz"},
        {"slew-rate",1000000,0.001,1000000000,"V/s"}, {"input-resistance",1000000,1,1e15,"ohm"},
        {"input-bias-current",0.000002,-1,1,"A"}, {"input-offset",0.01,-1000,1000,"V"},
        {"output-resistance",100,0.001,1e9,"ohm"}, {"output-current-limit",0.1,1e-9,1e6,"A"},
        {"supply-minimum",-12,-1e6,1e6,"V"}, {"supply-maximum",12,-1e6,1e6,"V"},
        {"minimum-output",-10,-1e6,1e6,"V"}, {"maximum-output",10,-1e6,1e6,"V"},
        {"max-step",1,1e-20,10,"V"}};
    solo_rig::CircuitAmplifierModel destination;
    REQUIRE(solo_rig::BindAuditedDynamicOpAmpModel(record, destination));
    REQUIRE(destination.kind == "generic-dynamic");
    REQUIRE(destination.model_version == 2U);
    REQUIRE(destination.gain_bandwidth_hz == 1000.0);
}
