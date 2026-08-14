#include "content/nonlinear_opamp_binding.hpp"

#include "dsp/nonlinear_opamp_dynamic.hpp"

#include <algorithm>
#include <string_view>

namespace solo_rig {
namespace {
const double* Parameter(const ModelAuditRecord& record, std::string_view id) noexcept {
    const auto it = std::find_if(record.parameters.begin(), record.parameters.end(),
                                 [id](const auto& parameter) { return parameter.id == id; });
    return it == record.parameters.end() ? nullptr : &it->default_value;
}
}

bool BindAuditedDynamicOpAmpModel(const ModelAuditRecord& record,
                                  CircuitAmplifierModel& destination) noexcept {
    if (record.kind != ModelKind::Opamp ||
        record.part_number != "self-authored-dynamic-opamp-no-commercial-part") return false;
    const auto value = [&record](std::string_view id) { return Parameter(record, id); };
    const double* gain = value("open-loop-gain");
    const double* gbw = value("gain-bandwidth");
    const double* slew = value("slew-rate");
    const double* rin = value("input-resistance");
    const double* bias = value("input-bias-current");
    const double* offset = value("input-offset");
    const double* rout = value("output-resistance");
    const double* limit = value("output-current-limit");
    const double* smin = value("supply-minimum");
    const double* smax = value("supply-maximum");
    const double* minimum = value("minimum-output");
    const double* maximum = value("maximum-output");
    const double* step = value("max-step");
    if (!gain || !gbw || !slew || !rin || !bias || !offset || !rout || !limit || !smin || !smax ||
        !minimum || !maximum || !step) return false;
    destination = CircuitAmplifierModel{
        .kind = "generic-dynamic", .model_version = 2U, .gain = *gain,
        .minimum_output = *minimum, .maximum_output = *maximum, .max_step = *step,
        .gain_bandwidth_hz = *gbw, .slew_rate = *slew, .input_resistance = *rin,
        .input_bias_current = *bias, .input_offset = *offset, .output_resistance = *rout,
        .output_current_limit = *limit, .supply_minimum = *smin, .supply_maximum = *smax};
    return ValidateNonlinearOpAmpDynamicModel({
        .gain = *gain, .gain_bandwidth_hz = *gbw, .slew_rate = *slew,
        .input_resistance = *rin, .input_bias_current = *bias, .input_offset = *offset,
        .output_resistance = *rout, .output_current_limit = *limit,
        .supply_minimum = *smin, .supply_maximum = *smax,
        .minimum_output = *minimum, .maximum_output = *maximum, .max_step = *step});
}

} // namespace solo_rig
