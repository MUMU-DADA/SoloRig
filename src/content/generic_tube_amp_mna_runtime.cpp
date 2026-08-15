#include "content/generic_tube_amp_mna_runtime.hpp"

#include <utility>

namespace solo_rig {

bool GenericTubeAmpMnaRuntime::Prepare(const GenericTubeAmpCompilePlan& plan,
                                       const CircuitNetlist& netlist,
                                       const ModelDatabase& model_database,
                                       double sample_rate_hz) noexcept {
    prepared_ = false;
    circuit_ = {};
    try {
        auto lowered = LowerGenericTubeAmpToMna(plan, netlist, model_database);
        if (!lowered.circuit.has_value() || !lowered.circuit->Prepare(sample_rate_hz)) return false;
        circuit_ = std::move(*lowered.circuit);
        prepared_ = true;
        return true;
    } catch (...) {
        prepared_ = false;
        circuit_ = {};
        return false;
    }
}

MnaSolveResult GenericTubeAmpMnaRuntime::Process(double input_voltage) noexcept {
    if (!prepared_) return {.error = MnaSolveError::NotPrepared};
    return circuit_.Solve(input_voltage);
}

void GenericTubeAmpMnaRuntime::Reset() noexcept {
    if (prepared_) circuit_.Reset();
}

} // namespace solo_rig
