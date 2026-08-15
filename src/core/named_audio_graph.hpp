#pragma once

#include "core/audio_buffer.hpp"
#include "core/audio_format.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace solo_rig {

inline constexpr std::size_t kMaximumNamedAudioPorts = 8U;
inline constexpr std::size_t kMaximumNamedAudioRoutes = 64U;
inline constexpr std::size_t kMaximumNamedAudioNodes = 16U;
inline constexpr std::size_t kMaximumNamedRoutesPerPort = 8U;

enum class NamedAudioPortDirection {
    Input,
    Output,
};

enum class NamedAudioPortDomain {
    AudioSignal,
    ControlSignal,
    DetectorInput,
    EmitterDrive,
    Resistance,
};

struct NamedAudioPortContract final {
    std::string_view name;
    NamedAudioPortDirection direction{NamedAudioPortDirection::Input};
    NamedAudioPortDomain domain{NamedAudioPortDomain::AudioSignal};
    // Zero means the graph's negotiated channel count. A non-zero value is a
    // fixed channel count, which is useful for scalar control/resistance ports.
    std::uint32_t channel_count{};
};

struct NamedAudioPortContractList final {
    std::array<NamedAudioPortContract, kMaximumNamedAudioPorts> ports{};
    std::size_t count{};
};

struct NamedAudioPortInput final {
    std::string_view name;
    NamedAudioPortDomain domain{NamedAudioPortDomain::AudioSignal};
    const AudioBuffer* buffer{};
};

struct NamedAudioPortOutput final {
    std::string_view name;
    NamedAudioPortDomain domain{NamedAudioPortDomain::AudioSignal};
    AudioBuffer* buffer{};
};

class NamedAudioPortProcessor {
public:
    virtual ~NamedAudioPortProcessor() = default;

    [[nodiscard]] virtual NamedAudioPortContractList InputPorts() const noexcept = 0;
    [[nodiscard]] virtual NamedAudioPortContractList OutputPorts() const noexcept = 0;
    [[nodiscard]] virtual bool Prepare(const AudioFormat& format,
                                       std::uint32_t oversampling) = 0;
    [[nodiscard]] virtual bool Process(std::span<const NamedAudioPortInput> inputs,
                                       std::span<NamedAudioPortOutput> outputs,
                                       std::uint32_t sample_count) noexcept = 0;
    [[nodiscard]] virtual std::uint32_t DeclaredLatencySamples() const noexcept = 0;
    virtual void Reset() noexcept = 0;
};

struct NamedAudioPortRouteSpec final {
    std::string source;
    std::string source_port;
    std::string destination;
    std::string destination_port;
    // A delayed route reads the source block from the previous Process call.
    // It is the only route type allowed to participate in a graph cycle.
    bool delayed{};
};

enum class NamedAudioGraphPreparationError {
    InvalidAudioFormat,
    InvalidOversampling,
    CapacityExceeded,
    DuplicateNode,
    InvalidNode,
    MissingProcessor,
    DuplicatePort,
    InvalidPort,
    DuplicateBoundaryPort,
    InvalidRoute,
    DuplicateRoute,
    UnknownEndpoint,
    UnknownPort,
    DirectionMismatch,
    IncompatibleDomain,
    ChannelMismatch,
    MissingPort,
    MissingInputRoute,
    MissingOutputRoute,
    CycleDetected,
    DelayedBoundaryRoute,
    ProcessorRejectedFormat,
    LatencyOverflow,
    BufferAllocationFailed,
};

class NamedPortAudioGraph final {
public:
    NamedPortAudioGraph() = default;
    NamedPortAudioGraph(NamedPortAudioGraph&&) noexcept = default;
    NamedPortAudioGraph& operator=(NamedPortAudioGraph&&) noexcept = default;
    NamedPortAudioGraph(const NamedPortAudioGraph&) = delete;
    NamedPortAudioGraph& operator=(const NamedPortAudioGraph&) = delete;

    [[nodiscard]] bool AddNode(std::string node_id,
                               std::unique_ptr<NamedAudioPortProcessor> processor);
    [[nodiscard]] bool AddInputPort(std::string name, NamedAudioPortDomain domain,
                                    std::uint32_t channel_count = 0U);
    [[nodiscard]] bool AddOutputPort(std::string name, NamedAudioPortDomain domain,
                                     std::uint32_t channel_count = 0U);
    [[nodiscard]] bool Connect(NamedAudioPortRouteSpec route);

    [[nodiscard]] std::vector<NamedAudioGraphPreparationError> Prepare(
        const AudioFormat& format, std::uint32_t oversampling = 1U);
    [[nodiscard]] bool Process(std::span<const AudioBuffer> inputs,
                               std::span<AudioBuffer> outputs,
                               std::uint32_t sample_count) noexcept;
    void Reset() noexcept;

    [[nodiscard]] std::size_t InputPortCount() const noexcept { return input_ports_.size(); }
    [[nodiscard]] std::size_t OutputPortCount() const noexcept { return output_ports_.size(); }
    [[nodiscard]] const NamedAudioPortContract& InputPort(std::size_t index) const noexcept;
    [[nodiscard]] const NamedAudioPortContract& OutputPort(std::size_t index) const noexcept;
    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept {
        return declared_latency_samples_;
    }
    [[nodiscard]] bool IsPrepared() const noexcept { return prepared_; }

private:
    static constexpr std::size_t kMissing = static_cast<std::size_t>(-1);

    struct DelayCompensator final {
        std::uint32_t delay_samples{};
        std::uint32_t write_position{};
        AudioBuffer delay_line;

        [[nodiscard]] bool Prepare(std::uint32_t channel_count,
                                   std::uint32_t capacity) noexcept;
        void Reset() noexcept;
        void AddFrom(const AudioBuffer& source, AudioBuffer& destination,
                     std::uint32_t sample_count) noexcept;
    };

    struct BoundaryPort final {
        NamedAudioPortContract contract;
        std::string name;
    };

    struct Node final {
        std::string id;
        std::unique_ptr<NamedAudioPortProcessor> processor;
        NamedAudioPortContractList input_contracts;
        NamedAudioPortContractList output_contracts;
        std::array<AudioBuffer, kMaximumNamedAudioPorts> input_buffers;
        std::array<AudioBuffer, kMaximumNamedAudioPorts> output_buffers;
        std::array<NamedAudioPortInput, kMaximumNamedAudioPorts> input_views;
        std::array<NamedAudioPortOutput, kMaximumNamedAudioPorts> output_views;
        std::vector<std::size_t> incoming_routes;
        std::vector<std::size_t> outgoing_routes;
        std::uint32_t input_latency_samples{};
        std::uint32_t cumulative_latency_samples{};
    };

    struct Route final {
        bool source_is_input{};
        bool destination_is_output{};
        bool delayed{};
        std::size_t source_boundary_index{kMissing};
        std::size_t destination_boundary_index{kMissing};
        std::size_t source_node_index{kMissing};
        std::size_t destination_node_index{kMissing};
        std::size_t source_port_index{kMissing};
        std::size_t destination_port_index{kMissing};
        DelayCompensator compensation;
        AudioBuffer delayed_buffer;
    };

    [[nodiscard]] std::optional<std::size_t> FindNode(std::string_view id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> FindInputPort(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<std::size_t> FindOutputPort(std::string_view name) const noexcept;
    [[nodiscard]] static std::optional<std::size_t> FindPort(
        const NamedAudioPortContractList& ports, std::string_view name) noexcept;
    [[nodiscard]] static bool Compatible(NamedAudioPortDomain source,
                                         NamedAudioPortDomain destination) noexcept;
    [[nodiscard]] static std::uint32_t ResolveChannelCount(
        const NamedAudioPortContract& contract, const AudioFormat& format) noexcept;
    [[nodiscard]] const AudioBuffer* SourceBuffer(
        const Route& route, std::span<const AudioBuffer> inputs) const noexcept;

    std::vector<Node> nodes_;
    std::vector<BoundaryPort> input_ports_;
    std::vector<BoundaryPort> output_ports_;
    std::vector<NamedAudioPortRouteSpec> route_specs_;
    std::vector<Route> routes_;
    std::vector<std::size_t> process_order_;
    AudioFormat format_{};
    std::uint32_t oversampling_{1U};
    std::uint32_t declared_latency_samples_{};
    bool prepared_{};
};

} // namespace solo_rig
