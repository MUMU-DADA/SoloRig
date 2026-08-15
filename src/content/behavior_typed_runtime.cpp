#include "content/behavior_typed_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace solo_rig {

namespace {

bool Authorized(const BehaviorGraphPlanNode& node) noexcept {
    if (node.lowering.status != WhiteboxPlanStatus::Ready) return false;
    return (node.lowering.kind == BehaviorKind::Lfo &&
            node.lowering.model_id == "generic-lfo-v1") ||
        (node.lowering.kind == BehaviorKind::Vca &&
         node.lowering.model_id == "generic-vca-v1");
}

std::size_t FindNode(const BehaviorTypedGraphPlan& plan, std::string_view id) noexcept {
    for (std::size_t index = 0U; index < plan.nodes.size(); ++index) {
        if (plan.nodes[index].id == id) return index;
    }
    return static_cast<std::size_t>(-1);
}

std::optional<BehaviorPortContract> FindPort(BehaviorKind kind,
                                             std::string_view name) noexcept {
    const auto ports = BehaviorPortsForKind(kind);
    for (std::size_t index = 0U; index < ports.count; ++index) {
        if (ports.ports[index].name == name) return ports.ports[index];
    }
    return std::nullopt;
}

} // namespace

TypedBehaviorRuntimeError TypedBehaviorGraphRuntime::PrepareSlot(
    Slot& slot, const BehaviorGraphPlanNode& node, double sample_rate_hz,
    std::uint32_t oversampling) noexcept {
    slot.kind = node.lowering.kind;
    if (!Authorized(node)) return TypedBehaviorRuntimeError::UnsupportedBlock;
    if (node.lowering.kind == BehaviorKind::Lfo) {
        slot.block.emplace<GenericLfoRuntime>();
        return std::get<GenericLfoRuntime>(slot.block).Prepare(
            GenericLfoModel{}, sample_rate_hz, oversampling) == GenericLfoError::None ?
            TypedBehaviorRuntimeError::None : TypedBehaviorRuntimeError::InvalidSampleRate;
    }
    slot.block.emplace<GenericVcaRuntime>();
    return std::get<GenericVcaRuntime>(slot.block).Prepare(
        GenericVcaModel{}, sample_rate_hz, oversampling) == GenericVcaError::None ?
        TypedBehaviorRuntimeError::None : TypedBehaviorRuntimeError::InvalidSampleRate;
}

TypedBehaviorRuntimeError TypedBehaviorGraphRuntime::Prepare(
    const BehaviorTypedGraphPlan& plan, double sample_rate_hz,
    std::uint32_t oversampling) noexcept {
    prepared_ = false;
    size_ = 0U;
    output_index_ = 0U;
    if (plan.nodes.empty() || plan.nodes.size() > kMaximumBehaviorGraphNodes ||
        plan.routes.size() > kMaximumBehaviorGraphRoutes) {
        return TypedBehaviorRuntimeError::CapacityExceeded;
    }
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return TypedBehaviorRuntimeError::InvalidSampleRate;
    }
    if (oversampling == 0U || oversampling > 16U) {
        return TypedBehaviorRuntimeError::InvalidOversampling;
    }
    if (plan.topological_order.size() != plan.nodes.size()) {
        return TypedBehaviorRuntimeError::InvalidPlan;
    }
    for (std::size_t index = 0U; index < plan.nodes.size(); ++index) {
        if (plan.topological_order[index] != plan.nodes[index].id ||
            !Authorized(plan.nodes[index])) {
            return !Authorized(plan.nodes[index]) ? TypedBehaviorRuntimeError::UnsupportedBlock :
                TypedBehaviorRuntimeError::InvalidPlan;
        }
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (plan.nodes[prior].id == plan.nodes[index].id) {
                return TypedBehaviorRuntimeError::InvalidPlan;
            }
        }
        const auto result = PrepareSlot(slots_[index], plan.nodes[index], sample_rate_hz,
                                        oversampling);
        if (result != TypedBehaviorRuntimeError::None) return result;
    }

    std::array<bool, kMaximumBehaviorGraphNodes> audio_seen{};
    std::array<bool, kMaximumBehaviorGraphNodes> control_seen{};
    std::array<bool, kMaximumBehaviorGraphNodes> output_seen{};
    bool external_input_seen = false;
    bool external_output_seen = false;
    for (const auto& route : plan.routes) {
        if (route.source.empty() || route.source_port.empty() || route.destination.empty() ||
            route.destination_port.empty() || route.source == route.destination) {
            return TypedBehaviorRuntimeError::InvalidPlan;
        }
        const auto source = route.source == "input" ? kExternal : FindNode(plan, route.source);
        const auto destination = route.destination == "output" ? kExternal :
            FindNode(plan, route.destination);
        if (route.source != "input" && source == kExternal) {
            return TypedBehaviorRuntimeError::InvalidPlan;
        }
        if (route.destination != "output" && destination == kExternal) {
            return TypedBehaviorRuntimeError::InvalidPlan;
        }
        const auto source_port = route.source == "input" ?
            std::optional<BehaviorPortContract>{} :
            FindPort(plan.nodes[source].lowering.kind, route.source_port);
        const auto destination_port = route.destination == "output" ?
            std::optional<BehaviorPortContract>{} :
            FindPort(plan.nodes[destination].lowering.kind, route.destination_port);
        if (route.source == "input") {
            if (route.source_port != "audio" || external_input_seen) {
                return TypedBehaviorRuntimeError::IncompatibleRoute;
            }
            external_input_seen = true;
        } else if (!source_port.has_value() ||
                   source_port->direction != BehaviorPortDirection::Output) {
            return TypedBehaviorRuntimeError::IncompatibleRoute;
        }
        if (route.destination == "output") {
            if (route.destination_port != "audio" || external_output_seen ||
                source == kExternal || plan.nodes[source].lowering.kind != BehaviorKind::Vca) {
                return TypedBehaviorRuntimeError::IncompatibleRoute;
            }
            external_output_seen = true;
            output_index_ = source;
            if (output_seen[source]) return TypedBehaviorRuntimeError::InvalidPlan;
            output_seen[source] = true;
        } else if (!destination_port.has_value() ||
                   destination_port->direction != BehaviorPortDirection::Input) {
            return TypedBehaviorRuntimeError::IncompatibleRoute;
        } else if (plan.nodes[destination].lowering.kind != BehaviorKind::Vca) {
            return TypedBehaviorRuntimeError::IncompatibleRoute;
        } else if (route.destination_port == "audio_in") {
            if (route.source != "input" || audio_seen[destination]) {
                return TypedBehaviorRuntimeError::IncompatibleRoute;
            }
            audio_seen[destination] = true;
            slots_[destination].audio_source = kExternal;
        } else if (route.destination_port == "control") {
            if (source == kExternal || plan.nodes[source].lowering.kind != BehaviorKind::Lfo ||
                control_seen[destination] || source >= destination) {
                return TypedBehaviorRuntimeError::IncompatibleRoute;
            }
            control_seen[destination] = true;
            slots_[destination].control_source = source;
            if (output_seen[source]) return TypedBehaviorRuntimeError::IncompatibleRoute;
            output_seen[source] = true;
        } else {
            return TypedBehaviorRuntimeError::IncompatibleRoute;
        }
    }
    if (!external_input_seen || !external_output_seen || output_index_ != plan.nodes.size() - 1U) {
        return TypedBehaviorRuntimeError::InvalidPlan;
    }
    for (std::size_t index = 0U; index < plan.nodes.size(); ++index) {
        if (plan.nodes[index].lowering.kind == BehaviorKind::Lfo) {
            if (!output_seen[index]) return TypedBehaviorRuntimeError::InvalidPlan;
        } else if (!audio_seen[index] || !control_seen[index] || !output_seen[index]) {
            return TypedBehaviorRuntimeError::InvalidPlan;
        }
    }
    size_ = plan.nodes.size();
    prepared_ = true;
    Reset();
    return TypedBehaviorRuntimeError::None;
}

TypedBehaviorRuntimeError TypedBehaviorGraphRuntime::SetSampleRate(double sample_rate_hz) noexcept {
    if (!prepared_) return TypedBehaviorRuntimeError::NotPrepared;
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return TypedBehaviorRuntimeError::InvalidSampleRate;
    }
    for (std::size_t index = 0U; index < size_; ++index) {
        const auto result = slots_[index].kind == BehaviorKind::Lfo ?
            (std::get<GenericLfoRuntime>(slots_[index].block).SetSampleRate(sample_rate_hz) ==
                GenericLfoError::None ? TypedBehaviorRuntimeError::None :
                TypedBehaviorRuntimeError::InvalidSampleRate) :
            (std::get<GenericVcaRuntime>(slots_[index].block).SetSampleRate(sample_rate_hz) ==
                GenericVcaError::None ? TypedBehaviorRuntimeError::None :
                TypedBehaviorRuntimeError::InvalidSampleRate);
        if (result != TypedBehaviorRuntimeError::None) return result;
    }
    return TypedBehaviorRuntimeError::None;
}

TypedBehaviorRuntimeError TypedBehaviorGraphRuntime::ProcessSlot(
    Slot& slot, double audio_input, double control_input, double& output) noexcept {
    if (slot.kind == BehaviorKind::Lfo) {
        return std::get<GenericLfoRuntime>(slot.block).Process(output) == GenericLfoError::None ?
            TypedBehaviorRuntimeError::None : TypedBehaviorRuntimeError::NonFinite;
    }
    return std::get<GenericVcaRuntime>(slot.block).Process(audio_input, control_input, output) ==
        GenericVcaError::None ? TypedBehaviorRuntimeError::None :
        TypedBehaviorRuntimeError::NonFinite;
}

TypedBehaviorRuntimeError TypedBehaviorGraphRuntime::Process(double audio_input,
                                                              double& audio_output) noexcept {
    audio_output = 0.0;
    if (!prepared_) return TypedBehaviorRuntimeError::NotPrepared;
    if (!std::isfinite(audio_input)) return TypedBehaviorRuntimeError::NonFinite;
    std::array<double, kMaximumBehaviorGraphNodes> values{};
    for (std::size_t index = 0U; index < size_; ++index) {
        const double control = slots_[index].control_source == kExternal ? 0.0 :
            values[slots_[index].control_source];
        const auto result = ProcessSlot(slots_[index], audio_input, control, values[index]);
        if (result != TypedBehaviorRuntimeError::None || !std::isfinite(values[index])) {
            audio_output = 0.0;
            return result == TypedBehaviorRuntimeError::None ?
                TypedBehaviorRuntimeError::NonFinite : result;
        }
    }
    audio_output = values[output_index_];
    return std::isfinite(audio_output) ? TypedBehaviorRuntimeError::None :
        TypedBehaviorRuntimeError::NonFinite;
}

void TypedBehaviorGraphRuntime::Reset() noexcept {
    for (std::size_t index = 0U; index < size_; ++index) {
        if (slots_[index].kind == BehaviorKind::Lfo) {
            std::get<GenericLfoRuntime>(slots_[index].block).Reset();
        } else {
            std::get<GenericVcaRuntime>(slots_[index].block).Reset();
        }
    }
}

} // namespace solo_rig
