#include "content/behavior_typed_runtime.hpp"

#include <array>
#include <cmath>
#include <optional>

namespace solo_rig {

namespace {

constexpr std::size_t kExternal = static_cast<std::size_t>(-1);
constexpr std::size_t kMissing = kExternal - 1U;

struct NodeValues final {
    double audio{};
    double control{};
    double resistance{};
};

bool Authorized(const BehaviorGraphPlanNode& node) noexcept {
    if (node.lowering.status != WhiteboxPlanStatus::Ready) return false;
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

std::size_t FindNode(const BehaviorTypedGraphPlan& plan, std::string_view id) noexcept {
    for (std::size_t index = 0U; index < plan.nodes.size(); ++index) {
        if (plan.nodes[index].id == id) return index;
    }
    return kMissing;
}

std::optional<std::size_t> PortIndex(BehaviorKind kind, std::string_view name) noexcept {
    const auto ports = BehaviorPortsForKind(kind);
    for (std::size_t index = 0U; index < ports.count; ++index) {
        if (ports.ports[index].name == name) return index;
    }
    return std::nullopt;
}

std::optional<BehaviorPortContract> FindPort(BehaviorKind kind,
                                             std::string_view name) noexcept {
    const auto ports = BehaviorPortsForKind(kind);
    for (std::size_t index = 0U; index < ports.count; ++index) {
        if (ports.ports[index].name == name) return ports.ports[index];
    }
    return std::nullopt;
}

std::optional<std::size_t> BoundaryInputIndex(std::string_view name) noexcept {
    if (name == "audio") return 0U;
    if (name == "control") return 1U;
    if (name == "detector") return 2U;
    if (name == "emitter") return 3U;
    return std::nullopt;
}

std::optional<std::size_t> BoundaryOutputIndex(std::string_view name) noexcept {
    if (name == "audio") return 0U;
    if (name == "control") return 1U;
    if (name == "resistance") return 2U;
    return std::nullopt;
}

bool Compatible(BehaviorPortDomain source, BehaviorPortDomain destination) noexcept {
    if (source == destination) return true;
    return source == BehaviorPortDomain::AudioSignal &&
        (destination == BehaviorPortDomain::DetectorInput ||
         destination == BehaviorPortDomain::EmitterDrive);
}

double ReadNodeValue(const NodeValues& value, BehaviorPortDomain domain) noexcept {
    switch (domain) {
    case BehaviorPortDomain::AudioSignal:
        return value.audio;
    case BehaviorPortDomain::ControlSignal:
        return value.control;
    case BehaviorPortDomain::Resistance:
        return value.resistance;
    case BehaviorPortDomain::DetectorInput:
    case BehaviorPortDomain::EmitterDrive:
        return 0.0;
    }
    return 0.0;
}

double ReadExternalValue(const TypedBehaviorInput& input,
                         BehaviorPortDomain domain) noexcept {
    switch (domain) {
    case BehaviorPortDomain::AudioSignal:
        return input.audio;
    case BehaviorPortDomain::ControlSignal:
        return input.control;
    case BehaviorPortDomain::DetectorInput:
        return input.detector;
    case BehaviorPortDomain::EmitterDrive:
        return input.emitter;
    case BehaviorPortDomain::Resistance:
        return 0.0;
    }
    return 0.0;
}

template <typename Error>
TypedBehaviorRuntimeError MapPrepareError(Error error) noexcept {
    return error == Error::None ? TypedBehaviorRuntimeError::None :
        TypedBehaviorRuntimeError::NonFinite;
}

template <typename Error>
TypedBehaviorRuntimeError MapProcessError(Error error) noexcept {
    return error == Error::None ? TypedBehaviorRuntimeError::None :
        TypedBehaviorRuntimeError::NonFinite;
}

} // namespace

TypedBehaviorRuntimeError TypedBehaviorGraphRuntime::PrepareSlot(
    Slot& slot, const BehaviorGraphPlanNode& node, double sample_rate_hz,
    std::uint32_t oversampling) noexcept {
    slot.kind = node.lowering.kind;
    slot.inputs.fill(Connection{});
    if (!Authorized(node)) return TypedBehaviorRuntimeError::UnsupportedBlock;
    switch (slot.kind) {
    case BehaviorKind::Bbd:
        slot.block.emplace<GenericBbdRuntime>();
        return MapPrepareError(std::get<GenericBbdRuntime>(slot.block).Prepare(
            GenericBbdModel{}, sample_rate_hz, oversampling));
    case BehaviorKind::Vco:
        slot.block.emplace<GenericVcoRuntime>();
        return MapPrepareError(std::get<GenericVcoRuntime>(slot.block).Prepare(
            GenericVcoModel{}, sample_rate_hz, oversampling));
    case BehaviorKind::Vca:
        slot.block.emplace<GenericVcaRuntime>();
        return MapPrepareError(std::get<GenericVcaRuntime>(slot.block).Prepare(
            GenericVcaModel{}, sample_rate_hz, oversampling));
    case BehaviorKind::Envelope:
        slot.block.emplace<GenericEnvelopeRuntime>();
        return MapPrepareError(std::get<GenericEnvelopeRuntime>(slot.block).Prepare(
            GenericEnvelopeModel{}, sample_rate_hz, oversampling));
    case BehaviorKind::Lfo:
        slot.block.emplace<GenericLfoRuntime>();
        return MapPrepareError(std::get<GenericLfoRuntime>(slot.block).Prepare(
            GenericLfoModel{}, sample_rate_hz, oversampling));
    case BehaviorKind::Optocoupler:
        slot.block.emplace<GenericOptocouplerRuntime>();
        return MapPrepareError(std::get<GenericOptocouplerRuntime>(slot.block).Prepare(
            GenericOptocouplerModel{}, sample_rate_hz, oversampling));
    case BehaviorKind::Spring:
        slot.block.emplace<GenericSpringRuntime>();
        return MapPrepareError(std::get<GenericSpringRuntime>(slot.block).Prepare(
            GenericSpringModel{}, sample_rate_hz, oversampling));
    }
    return TypedBehaviorRuntimeError::UnsupportedBlock;
}

TypedBehaviorRuntimeError TypedBehaviorGraphRuntime::Prepare(
    const BehaviorTypedGraphPlan& plan, double sample_rate_hz,
    std::uint32_t oversampling) noexcept {
    prepared_ = false;
    size_ = 0U;
    outputs_.fill(Connection{});
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
        if (plan.topological_order[index] != plan.nodes[index].id) {
            return TypedBehaviorRuntimeError::InvalidPlan;
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

    std::array<std::array<bool, 4U>, kMaximumBehaviorGraphNodes> input_seen{};
    std::array<std::array<bool, 4U>, kMaximumBehaviorGraphNodes> output_seen{};
    std::array<bool, 4U> external_input_seen{};
    std::array<bool, 3U> external_output_seen{};
    bool has_external_input = false;
    bool has_external_output = false;
    for (const auto& route : plan.routes) {
        if (route.source.empty() || route.source_port.empty() || route.destination.empty() ||
            route.destination_port.empty() || route.source == route.destination) {
            return TypedBehaviorRuntimeError::InvalidPlan;
        }
        const auto source = route.source == "input" ? kExternal : FindNode(plan, route.source);
        const auto destination = route.destination == "output" ? kExternal :
            FindNode(plan, route.destination);
        if (route.source != "input" && source == kMissing) {
            return TypedBehaviorRuntimeError::InvalidPlan;
        }
        if (route.destination != "output" && destination == kMissing) {
            return TypedBehaviorRuntimeError::InvalidPlan;
        }

        BehaviorPortContract source_port{};
        if (route.source == "input") {
            const auto input_index = BoundaryInputIndex(route.source_port);
            if (!input_index.has_value() || external_input_seen[*input_index]) {
                return TypedBehaviorRuntimeError::IncompatibleRoute;
            }
            external_input_seen[*input_index] = true;
            has_external_input = true;
            source_port = {route.source_port, BehaviorPortDirection::Output,
                route.source_port == "audio" ? BehaviorPortDomain::AudioSignal :
                route.source_port == "control" ? BehaviorPortDomain::ControlSignal :
                route.source_port == "detector" ? BehaviorPortDomain::DetectorInput :
                    BehaviorPortDomain::EmitterDrive};
        } else {
            const auto found = FindPort(plan.nodes[source].lowering.kind, route.source_port);
            if (!found.has_value() || found->direction != BehaviorPortDirection::Output) {
                return TypedBehaviorRuntimeError::IncompatibleRoute;
            }
            source_port = *found;
            const auto source_port_index = PortIndex(plan.nodes[source].lowering.kind,
                                                     route.source_port);
            if (!source_port_index.has_value() || output_seen[source][*source_port_index]) {
                return TypedBehaviorRuntimeError::IncompatibleRoute;
            }
            if (route.destination != "output" && source >= destination) {
                return TypedBehaviorRuntimeError::InvalidPlan;
            }
            output_seen[source][*source_port_index] = true;
        }

        if (route.destination == "output") {
            if (source == kExternal) return TypedBehaviorRuntimeError::IncompatibleRoute;
            const auto output_index = BoundaryOutputIndex(route.destination_port);
            if (!output_index.has_value() || external_output_seen[*output_index]) {
                return TypedBehaviorRuntimeError::IncompatibleRoute;
            }
            const auto destination_domain = route.destination_port == "audio" ?
                BehaviorPortDomain::AudioSignal : route.destination_port == "control" ?
                BehaviorPortDomain::ControlSignal : BehaviorPortDomain::Resistance;
            if (!Compatible(source_port.domain, destination_domain)) {
                return TypedBehaviorRuntimeError::IncompatibleRoute;
            }
            external_output_seen[*output_index] = true;
            has_external_output = true;
            outputs_[*output_index] = {.source = source, .domain = source_port.domain};
            continue;
        }

        const auto destination_port = FindPort(plan.nodes[destination].lowering.kind,
                                               route.destination_port);
        const auto destination_port_index = PortIndex(plan.nodes[destination].lowering.kind,
                                                      route.destination_port);
        if (!destination_port.has_value() || !destination_port_index.has_value() ||
            destination_port->direction != BehaviorPortDirection::Input) {
            return TypedBehaviorRuntimeError::IncompatibleRoute;
        }
        if (input_seen[destination][*destination_port_index] ||
            !Compatible(source_port.domain, destination_port->domain)) {
            return TypedBehaviorRuntimeError::IncompatibleRoute;
        }
        input_seen[destination][*destination_port_index] = true;
        slots_[destination].inputs[*destination_port_index] = {
            .source = source, .domain = source_port.domain};
    }

    if (!has_external_input || !has_external_output) {
        return TypedBehaviorRuntimeError::InvalidPlan;
    }
    for (std::size_t index = 0U; index < plan.nodes.size(); ++index) {
        const auto ports = BehaviorPortsForKind(plan.nodes[index].lowering.kind);
        for (std::size_t port = 0U; port < ports.count; ++port) {
            const auto seen = ports.ports[port].direction == BehaviorPortDirection::Input ?
                input_seen[index][port] : output_seen[index][port];
            if (!seen) return TypedBehaviorRuntimeError::InvalidPlan;
        }
    }
    size_ = plan.nodes.size();
    prepared_ = true;
    Reset();
    return TypedBehaviorRuntimeError::None;
}

TypedBehaviorRuntimeError TypedBehaviorGraphRuntime::SetSampleRate(
    double sample_rate_hz) noexcept {
    if (!prepared_) return TypedBehaviorRuntimeError::NotPrepared;
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
        return TypedBehaviorRuntimeError::InvalidSampleRate;
    }
    for (std::size_t index = 0U; index < size_; ++index) {
        TypedBehaviorRuntimeError result = TypedBehaviorRuntimeError::None;
        switch (slots_[index].kind) {
        case BehaviorKind::Bbd:
            result = MapPrepareError(std::get<GenericBbdRuntime>(slots_[index].block).
                SetSampleRate(sample_rate_hz));
            break;
        case BehaviorKind::Vco:
            result = MapPrepareError(std::get<GenericVcoRuntime>(slots_[index].block).
                SetSampleRate(sample_rate_hz));
            break;
        case BehaviorKind::Vca:
            result = MapPrepareError(std::get<GenericVcaRuntime>(slots_[index].block).
                SetSampleRate(sample_rate_hz));
            break;
        case BehaviorKind::Envelope:
            result = MapPrepareError(std::get<GenericEnvelopeRuntime>(slots_[index].block).
                SetSampleRate(sample_rate_hz));
            break;
        case BehaviorKind::Lfo:
            result = MapPrepareError(std::get<GenericLfoRuntime>(slots_[index].block).
                SetSampleRate(sample_rate_hz));
            break;
        case BehaviorKind::Optocoupler:
            result = MapPrepareError(std::get<GenericOptocouplerRuntime>(slots_[index].block).
                SetSampleRate(sample_rate_hz));
            break;
        case BehaviorKind::Spring:
            result = MapPrepareError(std::get<GenericSpringRuntime>(slots_[index].block).
                SetSampleRate(sample_rate_hz));
            break;
        }
        if (result != TypedBehaviorRuntimeError::None) return result;
    }
    return TypedBehaviorRuntimeError::None;
}

TypedBehaviorRuntimeError TypedBehaviorGraphRuntime::Process(
    const TypedBehaviorInput& input, TypedBehaviorOutput& output) noexcept {
    output = {};
    if (!prepared_) return TypedBehaviorRuntimeError::NotPrepared;
    if (!std::isfinite(input.audio) || !std::isfinite(input.control) ||
        !std::isfinite(input.detector) || !std::isfinite(input.emitter)) {
        return TypedBehaviorRuntimeError::NonFinite;
    }

    std::array<NodeValues, kMaximumBehaviorGraphNodes> values{};
    for (std::size_t index = 0U; index < size_; ++index) {
        const auto read_input = [&](std::size_t port, double& value) noexcept {
            const auto& connection = slots_[index].inputs[port];
            if (connection.source == kMissing) return false;
            value = connection.source == kExternal ? ReadExternalValue(input, connection.domain) :
                ReadNodeValue(values[connection.source], connection.domain);
            return std::isfinite(value);
        };
        TypedBehaviorRuntimeError result = TypedBehaviorRuntimeError::None;
        switch (slots_[index].kind) {
        case BehaviorKind::Bbd: {
            double audio{};
            if (!read_input(0U, audio)) return TypedBehaviorRuntimeError::NonFinite;
            result = MapProcessError(std::get<GenericBbdRuntime>(slots_[index].block).
                Process(audio, values[index].audio));
            break;
        }
        case BehaviorKind::Vco: {
            double control{};
            if (!read_input(0U, control)) return TypedBehaviorRuntimeError::NonFinite;
            result = MapProcessError(std::get<GenericVcoRuntime>(slots_[index].block).
                Process(control, values[index].audio));
            break;
        }
        case BehaviorKind::Vca: {
            double audio{};
            double control{};
            if (!read_input(0U, audio) || !read_input(1U, control)) {
                return TypedBehaviorRuntimeError::NonFinite;
            }
            result = MapProcessError(std::get<GenericVcaRuntime>(slots_[index].block).
                Process(audio, control, values[index].audio));
            break;
        }
        case BehaviorKind::Envelope: {
            double detector{};
            if (!read_input(0U, detector)) return TypedBehaviorRuntimeError::NonFinite;
            result = MapProcessError(std::get<GenericEnvelopeRuntime>(slots_[index].block).
                Process(detector, values[index].control));
            break;
        }
        case BehaviorKind::Lfo:
            result = MapProcessError(std::get<GenericLfoRuntime>(slots_[index].block).
                Process(values[index].control));
            break;
        case BehaviorKind::Optocoupler: {
            double emitter{};
            if (!read_input(0U, emitter)) return TypedBehaviorRuntimeError::NonFinite;
            result = MapProcessError(std::get<GenericOptocouplerRuntime>(slots_[index].block).
                Process(emitter, values[index].resistance));
            break;
        }
        case BehaviorKind::Spring: {
            double audio{};
            if (!read_input(0U, audio)) return TypedBehaviorRuntimeError::NonFinite;
            result = MapProcessError(std::get<GenericSpringRuntime>(slots_[index].block).
                Process(audio, values[index].audio));
            break;
        }
        }
        if (result != TypedBehaviorRuntimeError::None) return result;
        if (!std::isfinite(values[index].audio) || !std::isfinite(values[index].control) ||
            !std::isfinite(values[index].resistance)) {
            return TypedBehaviorRuntimeError::NonFinite;
        }
    }

    const auto read_output = [&](std::size_t index, double& value) noexcept {
        const auto& connection = outputs_[index];
        if (connection.source == kMissing || connection.source >= size_) return false;
        value = ReadNodeValue(values[connection.source], connection.domain);
        return std::isfinite(value);
    };
    for (std::size_t index = 0U; index < outputs_.size(); ++index) {
        if (outputs_[index].source == kMissing) continue;
        double* destination = index == 0U ? &output.audio :
            index == 1U ? &output.control : &output.resistance;
        if (!read_output(index, *destination)) {
            return TypedBehaviorRuntimeError::NonFinite;
        }
    }
    return TypedBehaviorRuntimeError::None;
}

TypedBehaviorRuntimeError TypedBehaviorGraphRuntime::Process(
    double audio_input, double& audio_output) noexcept {
    TypedBehaviorInput input{.audio = audio_input};
    TypedBehaviorOutput output{};
    const auto result = Process(input, output);
    audio_output = output.audio;
    return result;
}

void TypedBehaviorGraphRuntime::Reset() noexcept {
    for (std::size_t index = 0U; index < size_; ++index) {
        switch (slots_[index].kind) {
        case BehaviorKind::Bbd:
            std::get<GenericBbdRuntime>(slots_[index].block).Reset();
            break;
        case BehaviorKind::Vco:
            std::get<GenericVcoRuntime>(slots_[index].block).Reset();
            break;
        case BehaviorKind::Vca:
            std::get<GenericVcaRuntime>(slots_[index].block).Reset();
            break;
        case BehaviorKind::Envelope:
            std::get<GenericEnvelopeRuntime>(slots_[index].block).Reset();
            break;
        case BehaviorKind::Lfo:
            std::get<GenericLfoRuntime>(slots_[index].block).Reset();
            break;
        case BehaviorKind::Optocoupler:
            std::get<GenericOptocouplerRuntime>(slots_[index].block).Reset();
            break;
        case BehaviorKind::Spring:
            std::get<GenericSpringRuntime>(slots_[index].block).Reset();
            break;
        }
    }
}

} // namespace solo_rig
