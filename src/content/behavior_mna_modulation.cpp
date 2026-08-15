#include "content/behavior_mna_modulation.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace solo_rig {

TypedLfoMnaRuntimeError TypedLfoMnaRuntime::MapLfoError(
    GenericLfoError error) const noexcept {
    switch (error) {
    case GenericLfoError::None:
        return TypedLfoMnaRuntimeError::None;
    case GenericLfoError::InvalidSampleRate:
        return TypedLfoMnaRuntimeError::InvalidSampleRate;
    case GenericLfoError::InvalidOversampling:
        return TypedLfoMnaRuntimeError::InvalidOversampling;
    case GenericLfoError::NonFinite:
        return TypedLfoMnaRuntimeError::NonFinite;
    case GenericLfoError::InvalidModel:
        return TypedLfoMnaRuntimeError::InvalidModel;
    case GenericLfoError::NotPrepared:
        return TypedLfoMnaRuntimeError::NotPrepared;
    }
    return TypedLfoMnaRuntimeError::InvalidModel;
}

TypedLfoMnaRuntimeError TypedLfoMnaRuntime::MapMnaError(
    MnaSolveError error) const noexcept {
    switch (error) {
    case MnaSolveError::None:
        return TypedLfoMnaRuntimeError::None;
    case MnaSolveError::NotPrepared:
        return TypedLfoMnaRuntimeError::NotPrepared;
    case MnaSolveError::NonFinite:
        return TypedLfoMnaRuntimeError::NonFinite;
    case MnaSolveError::SingularMatrix:
    case MnaSolveError::NonConvergent:
        return TypedLfoMnaRuntimeError::MnaFailure;
    }
    return TypedLfoMnaRuntimeError::MnaFailure;
}

TypedLfoMnaRuntimeError TypedLfoMnaRuntime::Prepare(
    MnaPreparedCircuit circuit, std::string_view parameter_id,
    const TypedLfoMnaModel& model, double sample_rate_hz,
    std::uint32_t oversampling) noexcept {
    return Prepare(std::move(circuit), std::span<const std::string_view>(&parameter_id, 1U),
        model, sample_rate_hz, oversampling);
}

TypedLfoMnaRuntimeError TypedLfoMnaRuntime::Prepare(
    MnaPreparedCircuit circuit, std::span<const std::string_view> parameter_ids,
    const TypedLfoMnaModel& model, double sample_rate_hz,
    std::uint32_t oversampling) noexcept {
    prepared_ = false;
    control_count_ = 0U;
    control_indices_.fill(kInvalidControl);
    control_index_ = kInvalidControl;
    current_modulation_value_ = 0.0;
    if (parameter_ids.empty()) {
        return TypedLfoMnaRuntimeError::MissingControl;
    }
    if (parameter_ids.size() > control_indices_.size()) {
        return TypedLfoMnaRuntimeError::CapacityExceeded;
    }
    for (const auto parameter_id : parameter_ids) {
        if (parameter_id.empty()) return TypedLfoMnaRuntimeError::MissingControl;
    }
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return
            TypedLfoMnaRuntimeError::InvalidSampleRate;
    }
    if (oversampling == 0U || oversampling > 16U) {
        return TypedLfoMnaRuntimeError::InvalidOversampling;
    }
    if (!std::isfinite(model.modulation_min) || !std::isfinite(model.modulation_max) ||
        model.modulation_min > model.modulation_max ||
        model.modulation_min == model.modulation_max) {
        return TypedLfoMnaRuntimeError::InvalidModel;
    }
    for (std::size_t index = 0U; index < parameter_ids.size(); ++index) {
        const auto control = circuit.FindControl(parameter_ids[index]);
        if (!control.has_value()) return TypedLfoMnaRuntimeError::MissingControl;
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (control_indices_[prior] == *control) {
                return TypedLfoMnaRuntimeError::InvalidCircuit;
            }
        }
        control_indices_[index] = *control;
    }
    const auto lfo_result = lfo_.Prepare(model.lfo, sample_rate_hz, oversampling);
    if (lfo_result != GenericLfoError::None) return MapLfoError(lfo_result);
    if (!circuit.Prepare(sample_rate_hz)) {
        return TypedLfoMnaRuntimeError::InvalidCircuit;
    }
    circuit_ = std::move(circuit);
    model_ = model;
    control_count_ = parameter_ids.size();
    control_index_ = control_indices_[0U];
    prepared_ = true;
    Reset();
    return TypedLfoMnaRuntimeError::None;
}

TypedLfoMnaRuntimeError TypedLfoMnaRuntime::SetSampleRate(
    double sample_rate_hz) noexcept {
    if (!prepared_) return TypedLfoMnaRuntimeError::NotPrepared;
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return TypedLfoMnaRuntimeError::InvalidSampleRate;
    }
    const auto lfo_result = lfo_.SetSampleRate(sample_rate_hz);
    if (lfo_result != GenericLfoError::None) return MapLfoError(lfo_result);
    if (!circuit_.Prepare(sample_rate_hz)) {
        prepared_ = false;
        return TypedLfoMnaRuntimeError::InvalidCircuit;
    }
    Reset();
    return TypedLfoMnaRuntimeError::None;
}

TypedLfoMnaRuntimeError TypedLfoMnaRuntime::Process(
    double audio_input, double& audio_output) noexcept {
    audio_output = 0.0;
    if (!prepared_) return TypedLfoMnaRuntimeError::NotPrepared;
    if (!std::isfinite(audio_input)) return TypedLfoMnaRuntimeError::NonFinite;
    double control{};
    const auto lfo_result = lfo_.Process(control);
    if (lfo_result != GenericLfoError::None) return MapLfoError(lfo_result);
    const double normalized = 0.5 * (std::clamp(control, -1.0, 1.0) + 1.0);
    current_modulation_value_ = model_.modulation_min + normalized *
        (model_.modulation_max - model_.modulation_min);
    if (!std::isfinite(current_modulation_value_)) {
        return TypedLfoMnaRuntimeError::NonFinite;
    }
    for (std::size_t index = 0U; index < control_count_; ++index) {
        if (!circuit_.SetControlTarget(control_indices_[index], current_modulation_value_)) {
            return TypedLfoMnaRuntimeError::InvalidCircuit;
        }
    }
    const auto result = circuit_.Solve(audio_input);
    if (!result.Succeeded()) return MapMnaError(result.error);
    audio_output = result.output_voltage;
    return std::isfinite(audio_output) ? TypedLfoMnaRuntimeError::None :
        TypedLfoMnaRuntimeError::NonFinite;
}

void TypedLfoMnaRuntime::Reset() noexcept {
    if (!prepared_) return;
    lfo_.Reset();
    circuit_.Reset();
    current_modulation_value_ = model_.modulation_min + 0.5 *
        (model_.modulation_max - model_.modulation_min);
}

} // namespace solo_rig
