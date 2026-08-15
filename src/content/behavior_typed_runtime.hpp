#pragma once

#include "content/behavior_ports.hpp"
#include "dsp/generic_lfo.hpp"
#include "dsp/generic_vca.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace solo_rig {

enum class TypedBehaviorRuntimeError {
    None,
    InvalidPlan,
    InvalidSampleRate,
    InvalidOversampling,
    NotPrepared,
    NonFinite,
    UnsupportedBlock,
    IncompatibleRoute,
    CapacityExceeded,
};

class TypedBehaviorGraphRuntime final {
public:
    TypedBehaviorGraphRuntime() = default;
    TypedBehaviorGraphRuntime(TypedBehaviorGraphRuntime&&) noexcept = default;
    TypedBehaviorGraphRuntime& operator=(TypedBehaviorGraphRuntime&&) noexcept = default;
    TypedBehaviorGraphRuntime(const TypedBehaviorGraphRuntime&) = delete;
    TypedBehaviorGraphRuntime& operator=(const TypedBehaviorGraphRuntime&) = delete;

    [[nodiscard]] TypedBehaviorRuntimeError Prepare(const BehaviorTypedGraphPlan& plan,
                                                    double sample_rate_hz,
                                                    std::uint32_t oversampling) noexcept;
    [[nodiscard]] TypedBehaviorRuntimeError SetSampleRate(double sample_rate_hz) noexcept;
    [[nodiscard]] TypedBehaviorRuntimeError Process(double audio_input,
                                                    double& audio_output) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] std::size_t Size() const noexcept { return size_; }

private:
    using Block = std::variant<GenericLfoRuntime, GenericVcaRuntime>;
    static constexpr std::size_t kExternal = static_cast<std::size_t>(-1);

    struct Slot {
        Block block{};
        BehaviorKind kind{BehaviorKind::Lfo};
        std::size_t audio_source{kExternal};
        std::size_t control_source{kExternal};
    };

    [[nodiscard]] TypedBehaviorRuntimeError PrepareSlot(Slot& slot,
                                                        const BehaviorGraphPlanNode& node,
                                                        double sample_rate_hz,
                                                        std::uint32_t oversampling) noexcept;
    [[nodiscard]] TypedBehaviorRuntimeError ProcessSlot(Slot& slot,
                                                        double audio_input,
                                                        double control_input,
                                                        double& output) noexcept;

    std::array<Slot, kMaximumBehaviorGraphNodes> slots_{};
    std::size_t size_{};
    std::size_t output_index_{};
    bool prepared_{};
};

} // namespace solo_rig
