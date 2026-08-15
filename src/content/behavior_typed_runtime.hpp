#pragma once

#include "content/behavior_ports.hpp"
#include "dsp/generic_bbd.hpp"
#include "dsp/generic_envelope.hpp"
#include "dsp/generic_lfo.hpp"
#include "dsp/generic_optocoupler.hpp"
#include "dsp/generic_spring.hpp"
#include "dsp/generic_vco.hpp"
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

struct TypedBehaviorInput final {
    double audio{};
    double control{};
    double detector{};
    double emitter{};
};

struct TypedBehaviorOutput final {
    double audio{};
    double control{};
    double resistance{};
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
    [[nodiscard]] TypedBehaviorRuntimeError Process(const TypedBehaviorInput& input,
                                                    TypedBehaviorOutput& output) noexcept;
    [[nodiscard]] TypedBehaviorRuntimeError Process(double audio_input,
                                                    double& audio_output) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] std::size_t Size() const noexcept { return size_; }

private:
    using Block = std::variant<GenericBbdRuntime, GenericVcoRuntime, GenericVcaRuntime,
        GenericEnvelopeRuntime, GenericLfoRuntime, GenericOptocouplerRuntime,
        GenericSpringRuntime>;
    static constexpr std::size_t kExternal = static_cast<std::size_t>(-1);
    static constexpr std::size_t kMissing = kExternal - 1U;

    struct Connection final {
        std::size_t source{kMissing};
        BehaviorPortDomain domain{BehaviorPortDomain::AudioSignal};
    };

    struct Slot {
        Block block{};
        BehaviorKind kind{BehaviorKind::Lfo};
        std::array<Connection, 4U> inputs{};
    };

    [[nodiscard]] TypedBehaviorRuntimeError PrepareSlot(Slot& slot,
                                                        const BehaviorGraphPlanNode& node,
                                                        double sample_rate_hz,
                                                        std::uint32_t oversampling) noexcept;
    std::array<Slot, kMaximumBehaviorGraphNodes> slots_{};
    std::array<Connection, 3U> outputs_{};
    std::size_t size_{};
    bool prepared_{};
};

} // namespace solo_rig
