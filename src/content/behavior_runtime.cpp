#include "content/behavior_runtime.hpp"

#include <cmath>
#include <limits>
#include <string_view>

namespace solo_rig {

namespace {

constexpr std::size_t kExternalInput = std::numeric_limits<std::size_t>::max();

bool Authorized(const BehaviorGraphPlanNode& node) noexcept {
    if (node.lowering.status != WhiteboxPlanStatus::Ready) {
        return false;
    }
    switch (node.lowering.kind) {
    case BehaviorKind::Bbd:
        return node.lowering.model_id == "generic-bbd-v1";
    case BehaviorKind::Vco:
        return node.lowering.model_id == "generic-vco-v1";
    case BehaviorKind::Vca:
        return node.lowering.model_id == "generic-vca-v1";
    case BehaviorKind::Envelope:
        return node.lowering.model_id == "generic-envelope-v1";
    case BehaviorKind::Lfo:
        return node.lowering.model_id == "generic-lfo-v1";
    case BehaviorKind::Optocoupler:
        return node.lowering.model_id == "generic-optocoupler-v1";
    case BehaviorKind::Spring:
        return node.lowering.model_id == "generic-spring-v1";
    }
    return false;
}

bool IsSingleSignalCompatible(BehaviorKind kind) noexcept {
    return kind == BehaviorKind::Bbd || kind == BehaviorKind::Vco ||
        kind == BehaviorKind::Spring;
}

BehaviorRuntimeError MapBbd(GenericBbdError error) noexcept {
    return error == GenericBbdError::NonFinite ? BehaviorRuntimeError::NonFinite :
        error == GenericBbdError::InvalidSampleRate ? BehaviorRuntimeError::InvalidSampleRate :
        error == GenericBbdError::InvalidOversampling ? BehaviorRuntimeError::InvalidOversampling :
        error == GenericBbdError::None ? BehaviorRuntimeError::None :
        BehaviorRuntimeError::InvalidPlan;
}

} // namespace

BehaviorRuntimeError BehaviorGraphRuntime::PrepareSlot(
    Slot& slot, const BehaviorGraphPlanNode& node, double sample_rate_hz,
    std::uint32_t oversampling) noexcept {
    slot.kind = node.lowering.kind;
    if (!IsSingleSignalCompatible(node.lowering.kind)) {
        return BehaviorRuntimeError::IncompatibleBlock;
    }
    switch (node.lowering.kind) {
    case BehaviorKind::Bbd:
        slot.block.emplace<GenericBbdRuntime>();
        return MapBbd(std::get<GenericBbdRuntime>(slot.block).Prepare(
            GenericBbdModel{}, sample_rate_hz, oversampling));
    case BehaviorKind::Vco:
        slot.block.emplace<GenericVcoRuntime>();
        return std::get<GenericVcoRuntime>(slot.block).Prepare(
            GenericVcoModel{}, sample_rate_hz, oversampling) == GenericVcoError::None
            ? BehaviorRuntimeError::None : BehaviorRuntimeError::InvalidSampleRate;
    case BehaviorKind::Spring:
        slot.block.emplace<GenericSpringRuntime>();
        return std::get<GenericSpringRuntime>(slot.block).Prepare(
            GenericSpringModel{}, sample_rate_hz, oversampling) == GenericSpringError::None
            ? BehaviorRuntimeError::None : BehaviorRuntimeError::InvalidSampleRate;
    }
    return BehaviorRuntimeError::UnsupportedBlock;
}

BehaviorRuntimeError BehaviorGraphRuntime::Prepare(const BehaviorGraphPlan& plan,
                                                   double sample_rate_hz,
                                                   std::uint32_t oversampling) noexcept {
    prepared_ = false;
    size_ = 0U;
    output_index_ = 0U;
    if (plan.nodes.empty() || plan.nodes.size() > kMaximumBehaviorGraphNodes ||
        plan.routes.size() > kMaximumBehaviorGraphRoutes) {
        return BehaviorRuntimeError::CapacityExceeded;
    }
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return BehaviorRuntimeError::InvalidSampleRate;
    }
    if (oversampling == 0U || oversampling > 16U) {
        return BehaviorRuntimeError::InvalidOversampling;
    }
    if (plan.topological_order.size() != plan.nodes.size()) {
        return BehaviorRuntimeError::InvalidPlan;
    }
    for (std::size_t index = 0U; index < plan.nodes.size(); ++index) {
        if (!IsSingleSignalCompatible(plan.nodes[index].lowering.kind)) {
            return BehaviorRuntimeError::IncompatibleBlock;
        }
        if (!Authorized(plan.nodes[index])) {
            return BehaviorRuntimeError::UnsupportedBlock;
        }
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (plan.nodes[previous].id == plan.nodes[index].id) {
                return BehaviorRuntimeError::InvalidPlan;
            }
        }
        if (plan.topological_order[index] != plan.nodes[index].id) {
            return BehaviorRuntimeError::InvalidPlan;
        }
        slots_[index].source_index = kExternalInput;
        const auto result = PrepareSlot(slots_[index], plan.nodes[index], sample_rate_hz,
                                        oversampling);
        if (result != BehaviorRuntimeError::None) {
            return result;
        }
    }
    bool output_seen = false;
    bool input_seen = false;
    std::array<bool, kMaximumBehaviorGraphNodes> incoming_seen{};
    std::array<bool, kMaximumBehaviorGraphNodes> outgoing_seen{};
    for (const auto& route : plan.routes) {
        if (route.source == "output" || route.destination == "input" ||
            route.source.empty() || route.destination.empty() ||
            route.source == route.destination) {
            return BehaviorRuntimeError::InvalidPlan;
        }
        std::size_t destination = kExternalInput;
        for (std::size_t index = 0U; index < plan.nodes.size(); ++index) {
            if (plan.nodes[index].id == route.destination) {
                destination = index;
                break;
            }
        }
        if (route.destination == "output") {
            if (output_seen) return BehaviorRuntimeError::InvalidPlan;
            output_seen = true;
            bool source_found = false;
            for (std::size_t index = 0U; index < plan.nodes.size(); ++index) {
                if (plan.nodes[index].id == route.source) {
                    output_index_ = index;
                    source_found = true;
                }
            }
            if (!source_found || outgoing_seen[output_index_]) return BehaviorRuntimeError::InvalidPlan;
            outgoing_seen[output_index_] = true;
            continue;
        }
        if (destination == kExternalInput) return BehaviorRuntimeError::InvalidPlan;
        if (incoming_seen[destination]) return BehaviorRuntimeError::InvalidPlan;
        if (route.source == "input") {
            if (input_seen || destination != 0U) return BehaviorRuntimeError::InvalidPlan;
            input_seen = true;
            incoming_seen[destination] = true;
            slots_[destination].source_index = kExternalInput;
            continue;
        }
        std::size_t source = kExternalInput;
        for (std::size_t index = 0U; index < plan.nodes.size(); ++index) {
            if (plan.nodes[index].id == route.source) source = index;
        }
        if (source == kExternalInput || source >= destination ||
            outgoing_seen[source]) {
            return BehaviorRuntimeError::InvalidPlan;
        }
        incoming_seen[destination] = true;
        outgoing_seen[source] = true;
        slots_[destination].source_index = source;
    }
    if (!input_seen || !output_seen || output_index_ != plan.nodes.size() - 1U) {
        return BehaviorRuntimeError::InvalidPlan;
    }
    for (std::size_t index = 0U; index < plan.nodes.size(); ++index) {
        if (!incoming_seen[index] || !outgoing_seen[index]) return BehaviorRuntimeError::InvalidPlan;
    }
    size_ = plan.nodes.size();
    sample_rate_hz_ = sample_rate_hz;
    oversampling_ = oversampling;
    prepared_ = true;
    Reset();
    return BehaviorRuntimeError::None;
}

BehaviorRuntimeError BehaviorGraphRuntime::SetSampleRate(double sample_rate_hz) noexcept {
    if (!prepared_) return BehaviorRuntimeError::NotPrepared;
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return BehaviorRuntimeError::InvalidSampleRate;
    }
    for (std::size_t index = 0U; index < size_; ++index) {
        BehaviorRuntimeError result = BehaviorRuntimeError::None;
        switch (slots_[index].kind) {
        case BehaviorKind::Bbd: result = MapBbd(std::get<GenericBbdRuntime>(slots_[index].block).SetSampleRate(sample_rate_hz)); break;
        case BehaviorKind::Vco: result = std::get<GenericVcoRuntime>(slots_[index].block).SetSampleRate(sample_rate_hz) == GenericVcoError::None ? BehaviorRuntimeError::None : BehaviorRuntimeError::InvalidSampleRate; break;
        case BehaviorKind::Spring: result = std::get<GenericSpringRuntime>(slots_[index].block).SetSampleRate(sample_rate_hz) == GenericSpringError::None ? BehaviorRuntimeError::None : BehaviorRuntimeError::InvalidSampleRate; break;
        }
        if (result != BehaviorRuntimeError::None) return result;
    }
    sample_rate_hz_ = sample_rate_hz;
    return BehaviorRuntimeError::None;
}

BehaviorRuntimeError BehaviorGraphRuntime::ProcessSlot(Slot& slot, double input,
                                                        double& output) noexcept {
    switch (slot.kind) {
    case BehaviorKind::Bbd: return MapBbd(std::get<GenericBbdRuntime>(slot.block).Process(input, output));
    case BehaviorKind::Vco: return std::get<GenericVcoRuntime>(slot.block).Process(input, output) == GenericVcoError::None ? BehaviorRuntimeError::None : BehaviorRuntimeError::NonFinite;
    case BehaviorKind::Spring: return std::get<GenericSpringRuntime>(slot.block).Process(input, output) == GenericSpringError::None ? BehaviorRuntimeError::None : BehaviorRuntimeError::NonFinite;
    }
    return BehaviorRuntimeError::UnsupportedBlock;
}

BehaviorRuntimeError BehaviorGraphRuntime::Process(double input, double& output) noexcept {
    output = 0.0;
    if (!prepared_) return BehaviorRuntimeError::NotPrepared;
    if (!std::isfinite(input)) return BehaviorRuntimeError::NonFinite;
    std::array<double, kMaximumBehaviorGraphNodes> values{};
    for (std::size_t index = 0U; index < size_; ++index) {
        const double source = slots_[index].source_index == kExternalInput ? input :
            values[slots_[index].source_index];
        const auto result = ProcessSlot(slots_[index], source, values[index]);
        if (result != BehaviorRuntimeError::None || !std::isfinite(values[index])) {
            output = 0.0;
            return result == BehaviorRuntimeError::None ? BehaviorRuntimeError::NonFinite : result;
        }
    }
    output = values[output_index_];
    return std::isfinite(output) ? BehaviorRuntimeError::None : BehaviorRuntimeError::NonFinite;
}

void BehaviorGraphRuntime::Reset() noexcept {
    for (std::size_t index = 0U; index < size_; ++index) {
        switch (slots_[index].kind) {
        case BehaviorKind::Bbd: std::get<GenericBbdRuntime>(slots_[index].block).Reset(); break;
        case BehaviorKind::Vco: std::get<GenericVcoRuntime>(slots_[index].block).Reset(); break;
        case BehaviorKind::Spring: std::get<GenericSpringRuntime>(slots_[index].block).Reset(); break;
        }
    }
}

} // namespace solo_rig
