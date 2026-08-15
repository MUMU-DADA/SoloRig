#pragma once

#include "content/module_runtime.hpp"
#include "control/preset_controller.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace solo_rig {

enum class RigBuildError {
    EmptyModuleId,
    UnknownModuleId,
    MissingModuleAsset,
    ParameterMismatch,
    ProcessorInstantiationFailed,
    InvalidTopology,
    GraphPreparationFailed,
};

using RigModuleFactory = std::function<std::variant<std::unique_ptr<ModuleRuntimeProcessor>, RigBuildError>(
    const PresetNode& node)>;

class RigModuleRegistry {
public:
    RigModuleRegistry();

    [[nodiscard]] std::optional<RigBuildError> Register(std::string module_id, RigModuleFactory factory);
    [[nodiscard]] std::optional<RigBuildError> RegisterPackage(ModulePackage package);
    [[nodiscard]] bool Contains(std::string_view module_id) const;
    [[nodiscard]] std::variant<std::unique_ptr<ModuleRuntimeProcessor>, RigBuildError> Instantiate(
        const PresetNode& node) const;

private:
    std::map<std::string, RigModuleFactory, std::less<>> factories_;
    std::map<std::string, ModulePackage, std::less<>> packages_;
};

class RigRuntimeBuilder {
public:
    [[nodiscard]] std::variant<PreparedPreset, RigBuildError> Build(const PreparedPreset& prepared,
                                                                     const RigModuleRegistry& registry,
                                                                     const AudioFormat& format) const;
};

class RigRuntime {
public:
    RigRuntime(RealtimePresetMailbox& mailbox, AudioFormat format, PresetTransitionPolicy transition = {});

    // Allocates all graph-independent transition buffers. Call off the audio
    // thread before Process; Process itself is noexcept and allocation-free.
    [[nodiscard]] bool Prepare();
    // Must be called only after the host has stopped invoking Process. Retires
    // current, target, and pending ownership before the runtime is rebuilt.
    void Shutdown() noexcept;
    [[nodiscard]] bool Process(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept;
    [[nodiscard]] bool SetParameter(std::string_view instance_id,
                                    std::string_view parameter_id,
                                    float value) noexcept;
    // Control-thread diagnostic snapshot. Call only while the host has
    // stopped Process; the snapshot reports the active graph, not a pending
    // crossfade target, and allocates its returned vector on the caller side.
    [[nodiscard]] std::vector<AudioGraph::NodePerformance> PerformanceSnapshot() const;
    [[nodiscard]] bool IsPrepared() const noexcept;
    [[nodiscard]] std::uint32_t TransitionSamples() const noexcept;
    [[nodiscard]] bool InTransition() const noexcept;

private:
    void ObservePending() noexcept;
    void FinishTransition() noexcept;

    RealtimePresetMailbox& mailbox_;
    AudioFormat format_;
    PresetTransitionPolicy transition_policy_;
    AudioBuffer outgoing_buffer_;
    AudioBuffer incoming_buffer_;
    const PreparedPreset* current_{};
    const PreparedPreset* target_{};
    std::uint32_t transition_total_samples_{};
    std::uint32_t transition_position_{};
    bool prepared_{};
};

} // namespace solo_rig
