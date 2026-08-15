#pragma once

#include "content/generic_tube_amp_mna_lowering.hpp"

namespace solo_rig {

class GenericTubeAmpMnaRuntime final {
public:
    [[nodiscard]] bool Prepare(const GenericTubeAmpCompilePlan& plan,
                               const CircuitNetlist& netlist,
                               const ModelDatabase& model_database,
                               double sample_rate_hz) noexcept;
    [[nodiscard]] MnaSolveResult Process(double input_voltage) noexcept;
    void Reset() noexcept;
    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] static constexpr RealtimeContract ProcessContract() noexcept {
        return MnaPreparedCircuit::ProcessContract();
    }

private:
    MnaPreparedCircuit circuit_;
    bool prepared_{};
};

} // namespace solo_rig
