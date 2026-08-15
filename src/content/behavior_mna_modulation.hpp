#pragma once

#include "content/circuit_v2.hpp"
#include "dsp/generic_lfo.hpp"
#include "dsp/mna_solver.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace solo_rig {

enum class TypedLfoMnaRuntimeError {
    None,
    InvalidCircuit,
    InvalidModel,
    InvalidSampleRate,
    InvalidOversampling,
    CapacityExceeded,
    MissingControl,
    NotPrepared,
    NonFinite,
    MnaFailure,
};

struct TypedLfoMnaModel final {
    GenericLfoModel lfo{};
    double modulation_min{};
    double modulation_max{1.0};
};

class TypedLfoMnaRuntime final {
public:
    TypedLfoMnaRuntime() = default;
    TypedLfoMnaRuntime(TypedLfoMnaRuntime&&) noexcept = default;
    TypedLfoMnaRuntime& operator=(TypedLfoMnaRuntime&&) noexcept = default;
    TypedLfoMnaRuntime(const TypedLfoMnaRuntime&) = delete;
    TypedLfoMnaRuntime& operator=(const TypedLfoMnaRuntime&) = delete;

    [[nodiscard]] TypedLfoMnaRuntimeError Prepare(
        MnaPreparedCircuit circuit, std::string_view parameter_id,
        const TypedLfoMnaModel& model, double sample_rate_hz,
        std::uint32_t oversampling) noexcept;
    [[nodiscard]] TypedLfoMnaRuntimeError Prepare(
        MnaPreparedCircuit circuit, std::span<const std::string_view> parameter_ids,
        const TypedLfoMnaModel& model, double sample_rate_hz,
        std::uint32_t oversampling) noexcept;
    [[nodiscard]] TypedLfoMnaRuntimeError SetSampleRate(double sample_rate_hz) noexcept;
    [[nodiscard]] TypedLfoMnaRuntimeError Process(double audio_input,
                                                  double& audio_output) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] std::size_t ControlIndex() const noexcept { return control_index_; }
    [[nodiscard]] std::size_t ControlCount() const noexcept { return control_count_; }
    [[nodiscard]] std::size_t ControlIndex(std::size_t index) const noexcept {
        return index < control_count_ ? control_indices_[index] : kInvalidControl;
    }
    [[nodiscard]] double CurrentModulationValue() const noexcept {
        return current_modulation_value_;
    }

private:
    static constexpr std::size_t kInvalidControl = static_cast<std::size_t>(-1);

    [[nodiscard]] TypedLfoMnaRuntimeError MapLfoError(GenericLfoError error) const noexcept;
    [[nodiscard]] TypedLfoMnaRuntimeError MapMnaError(MnaSolveError error) const noexcept;

    GenericLfoRuntime lfo_{};
    MnaPreparedCircuit circuit_{};
    TypedLfoMnaModel model_{};
    std::array<std::size_t, 8U> control_indices_{};
    std::size_t control_count_{};
    std::size_t control_index_{kInvalidControl};
    double current_modulation_value_{};
    bool prepared_{};
};

} // namespace solo_rig
