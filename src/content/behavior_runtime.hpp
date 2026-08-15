#pragma once

#include "content/behavior_graph.hpp"
#include "dsp/generic_bbd.hpp"
#include "dsp/generic_spring.hpp"
#include "dsp/generic_vco.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace solo_rig {

enum class BehaviorRuntimeError {
    None,
    InvalidPlan,
    InvalidSampleRate,
    InvalidOversampling,
    NotPrepared,
    NonFinite,
    UnsupportedBlock,
    IncompatibleBlock,
    CapacityExceeded,
};

class BehaviorGraphRuntime final {
public:
    BehaviorGraphRuntime() = default;
    BehaviorGraphRuntime(BehaviorGraphRuntime&&) noexcept = default;
    BehaviorGraphRuntime& operator=(BehaviorGraphRuntime&&) noexcept = default;
    BehaviorGraphRuntime(const BehaviorGraphRuntime&) = delete;
    BehaviorGraphRuntime& operator=(const BehaviorGraphRuntime&) = delete;

    [[nodiscard]] BehaviorRuntimeError Prepare(const BehaviorGraphPlan& plan,
                                               double sample_rate_hz,
                                               std::uint32_t oversampling) noexcept;
    [[nodiscard]] BehaviorRuntimeError SetSampleRate(double sample_rate_hz) noexcept;
    [[nodiscard]] BehaviorRuntimeError Process(double input, double& output) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }
    [[nodiscard]] std::size_t Size() const noexcept { return size_; }

private:
    using Block = std::variant<GenericBbdRuntime, GenericVcoRuntime, GenericSpringRuntime>;

    struct Slot {
        Block block{};
        BehaviorKind kind{BehaviorKind::Bbd};
        std::size_t source_index{};
    };

    [[nodiscard]] BehaviorRuntimeError PrepareSlot(Slot& slot,
                                                    const BehaviorGraphPlanNode& node,
                                                    double sample_rate_hz,
                                                    std::uint32_t oversampling) noexcept;
    [[nodiscard]] BehaviorRuntimeError ProcessSlot(Slot& slot,
                                                    double input,
                                                    double& output) noexcept;

    std::array<Slot, kMaximumBehaviorGraphNodes> slots_{};
    std::size_t size_{};
    std::size_t output_index_{};
    double sample_rate_hz_{};
    std::uint32_t oversampling_{1U};
    bool prepared_{};
};

} // namespace solo_rig
