#pragma once

#include "content/behavior_mna_composite.hpp"
#include "content/behavior_typed_runtime.hpp"
#include "core/named_audio_graph.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace solo_rig {

class TypedBehaviorPortProcessor final : public NamedAudioPortProcessor {
public:
    explicit TypedBehaviorPortProcessor(BehaviorTypedGraphPlan plan,
                                        std::uint32_t oversampling = 1U);

    [[nodiscard]] NamedAudioPortContractList InputPorts() const noexcept override { return inputs_; }
    [[nodiscard]] NamedAudioPortContractList OutputPorts() const noexcept override { return outputs_; }
    [[nodiscard]] bool Prepare(const AudioFormat& format,
                               std::uint32_t oversampling) override;
    [[nodiscard]] bool Process(std::span<const NamedAudioPortInput> inputs,
                               std::span<NamedAudioPortOutput> outputs,
                               std::uint32_t sample_count) noexcept override;
    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override { return 0U; }
    void Reset() noexcept override;

private:
    [[nodiscard]] static NamedAudioPortDomain InputDomain(std::string_view name) noexcept;
    [[nodiscard]] static NamedAudioPortDomain OutputDomain(std::string_view name) noexcept;
    void AddInput(std::string_view name, NamedAudioPortDomain domain) noexcept;
    void AddOutput(std::string_view name, NamedAudioPortDomain domain) noexcept;

    BehaviorTypedGraphPlan plan_;
    NamedAudioPortContractList inputs_;
    NamedAudioPortContractList outputs_;
    std::array<std::int32_t, 4U> input_indices_{};
    std::array<std::int32_t, 3U> output_indices_{};
    TypedBehaviorGraphRuntime runtime_;
    std::uint32_t requested_oversampling_{1U};
    bool valid_{};
    bool prepared_{};
};

class TypedBehaviorMnaPortProcessor final : public NamedAudioPortProcessor {
public:
    TypedBehaviorMnaPortProcessor(
        BehaviorTypedGraphPlan plan,
        MnaPreparedCircuit circuit,
        std::span<const TypedBehaviorMnaBinding> bindings,
        std::uint32_t oversampling = 1U);

    [[nodiscard]] NamedAudioPortContractList InputPorts() const noexcept override { return inputs_; }
    [[nodiscard]] NamedAudioPortContractList OutputPorts() const noexcept override { return outputs_; }
    [[nodiscard]] bool Prepare(const AudioFormat& format,
                               std::uint32_t oversampling) override;
    [[nodiscard]] bool Process(std::span<const NamedAudioPortInput> inputs,
                               std::span<NamedAudioPortOutput> outputs,
                               std::uint32_t sample_count) noexcept override;
    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override { return 0U; }
    void Reset() noexcept override;

private:
    void AddInput(std::string_view name, NamedAudioPortDomain domain) noexcept;

    BehaviorTypedGraphPlan plan_;
    MnaPreparedCircuit circuit_;
    std::array<TypedBehaviorMnaBinding, TypedBehaviorMnaRuntime::kMaximumBindings> bindings_{};
    std::size_t binding_count_{};
    NamedAudioPortContractList inputs_;
    NamedAudioPortContractList outputs_;
    std::array<std::int32_t, 4U> input_indices_{};
    TypedBehaviorMnaRuntime runtime_;
    std::uint32_t requested_oversampling_{1U};
    bool valid_{};
    bool prepared_{};
};

} // namespace solo_rig
