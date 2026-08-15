#include "content/module_runtime.hpp"

#include "dsp/digital_delay.hpp"
#include "dsp/digital_dynamics.hpp"
#include "dsp/digital_filter.hpp"
#include "dsp/digital_pitch_shift.hpp"
#include "dsp/diode_clipper.hpp"
#include "dsp/impulse_response.hpp"
#include "dsp/mna_solver.hpp"
#include "dsp/nam_processor.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace solo_rig {

namespace {

struct ParameterSlot {
    std::string id;
    float minimum{};
    float maximum{};
    float value{};
};

class ParameterState {
public:
    explicit ParameterState(const std::vector<EffectParameter>& parameters) {
        slots_.reserve(parameters.size());
        for (const auto& parameter : parameters) {
            slots_.push_back({.id = parameter.id,
                              .minimum = parameter.minimum,
                              .maximum = parameter.maximum,
                              .value = parameter.default_value});
        }
    }

    [[nodiscard]] std::optional<std::size_t> Find(std::string_view id) const noexcept {
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            if (slots_[index].id == id) {
                return index;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool Set(std::string_view id, float value) noexcept {
        const auto index = Find(id);
        if (!index.has_value() || !std::isfinite(value) || value < slots_[*index].minimum || value > slots_[*index].maximum) {
            return false;
        }
        slots_[*index].value = value;
        return true;
    }

    [[nodiscard]] std::optional<float> Value(std::string_view id) const noexcept {
        const auto index = Find(id);
        return index.has_value() ? std::optional<float>(slots_[*index].value) : std::nullopt;
    }

    [[nodiscard]] float Value(std::optional<std::size_t> index, float fallback) const noexcept {
        return index.has_value() ? slots_[*index].value : fallback;
    }

    [[nodiscard]] std::optional<float> Replace(std::string_view id, float value) noexcept {
        const auto index = Find(id);
        if (!index.has_value() || !std::isfinite(value) || value < slots_[*index].minimum || value > slots_[*index].maximum) {
            return std::nullopt;
        }
        const float previous = slots_[*index].value;
        slots_[*index].value = value;
        return previous;
    }

    void Restore(std::string_view id, float value) noexcept {
        const auto index = Find(id);
        if (index.has_value()) {
            slots_[*index].value = value;
        }
    }

private:
    std::vector<ParameterSlot> slots_;
};

bool HasNodes(const AnalogCircuitComponent& component, std::string_view left, std::string_view right) {
    return component.nodes.size() == 2U &&
           ((component.nodes[0] == left && component.nodes[1] == right) ||
            (component.nodes[0] == right && component.nodes[1] == left));
}

class AnalogCircuitRuntime final : public ModuleRuntimeProcessor {
public:
    AnalogCircuitRuntime(std::uint32_t declared_latency_samples,
                         ParameterState parameters,
                         DiodeClipperSettings settings)
        : declared_latency_samples_(declared_latency_samples), parameters_(std::move(parameters)), settings_(settings), clipper_(settings) {}

    bool Prepare(const AudioFormat& format) override {
        return clipper_.Prepare(format);
    }

    void Process(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept override {
        clipper_.Process(input, output, sample_count);
    }

    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override {
        return declared_latency_samples_;
    }

    [[nodiscard]] bool SetParameter(std::string_view parameter_id, float value) noexcept override {
        const auto previous = parameters_.Replace(parameter_id, value);
        if (!previous.has_value()) {
            return false;
        }
        const auto candidate = BuildSettings();
        if (!clipper_.SetSettings(candidate)) {
            parameters_.Restore(parameter_id, *previous);
            return false;
        }
        settings_ = candidate;
        return true;
    }

    [[nodiscard]] std::optional<float> ParameterValue(std::string_view parameter_id) const noexcept override {
        return parameters_.Value(parameter_id);
    }

private:
    [[nodiscard]] DiodeClipperSettings BuildSettings() const noexcept {
        auto result = settings_;
        result.drive = parameters_.Value("drive").value_or(result.drive);
        result.output_gain = parameters_.Value("outputGain").value_or(result.output_gain);
        result.diode_threshold = parameters_.Value("diodeThreshold").value_or(result.diode_threshold);
        return result;
    }

    std::uint32_t declared_latency_samples_{};
    ParameterState parameters_;
    DiodeClipperSettings settings_;
    DiodeClipper clipper_;
};

struct BoundDigitalOperation {
    DigitalOperationType type{DigitalOperationType::Bypass};
    std::optional<std::size_t> parameter_index;
    std::size_t state_index{};
};

class DigitalAlgorithmRuntime final : public ModuleRuntimeProcessor {
public:
    DigitalAlgorithmRuntime(std::uint32_t declared_latency_samples,
                            ParameterState parameters,
                            std::vector<BoundDigitalOperation> operations,
                            bool uses_delay,
                            std::size_t filter_count,
                            std::size_t dynamics_count,
                            std::size_t pitch_shift_count)
        : declared_latency_samples_(declared_latency_samples), parameters_(std::move(parameters)),
          operations_(std::move(operations)), filters_(filter_count), dynamics_(dynamics_count),
          pitch_shifts_(pitch_shift_count) {
        if (uses_delay) {
            delay_.emplace();
        }
    }

    bool Prepare(const AudioFormat& format) override {
        if (!format.Validate().empty()) {
            return false;
        }
        scratch_a_ = AudioBuffer(format.channel_count, format.max_block_size);
        scratch_b_ = AudioBuffer(format.channel_count, format.max_block_size);
        if (delay_.has_value() && (!ConfigureDelay() || !delay_->Prepare(format))) {
            return false;
        }
        if (!ConfigureSpecializedOperations() ||
            std::ranges::any_of(filters_, [&format](DigitalFilter& filter) { return !filter.Prepare(format); }) ||
            std::ranges::any_of(dynamics_, [&format](DigitalDynamics& dynamics) { return !dynamics.Prepare(format); }) ||
            std::ranges::any_of(pitch_shifts_, [&format](DigitalPitchShift& pitch_shift) {
                return !pitch_shift.Prepare(format);
            })) {
            prepared_ = false;
            return false;
        }
        prepared_ = scratch_a_.IsValid() && scratch_b_.IsValid();
        return prepared_;
    }

    void Process(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept override {
        if (!prepared_ || sample_count > scratch_a_.Capacity() || input.ChannelCount() != scratch_a_.ChannelCount() ||
            output.ChannelCount() != scratch_a_.ChannelCount()) {
            return;
        }

        const AudioBuffer* current = &input;
        AudioBuffer* next = &scratch_a_;
        for (const auto& operation : operations_) {
            switch (operation.type) {
            case DigitalOperationType::Gain:
                ApplyGain(*current, *next, parameters_.Value(operation.parameter_index, 1.0F), sample_count);
                break;
            case DigitalOperationType::Waveshaper:
                ApplyWaveshaper(*current, *next, parameters_.Value(operation.parameter_index, 1.0F), sample_count);
                break;
            case DigitalOperationType::Filter:
                filters_[operation.state_index].Process(*current, *next, sample_count);
                break;
            case DigitalOperationType::Dynamics:
                dynamics_[operation.state_index].Process(*current, *next, sample_count);
                break;
            case DigitalOperationType::PitchShift:
                pitch_shifts_[operation.state_index].Process(*current, *next, sample_count);
                break;
            case DigitalOperationType::Delay:
                delay_->Process(*current, *next, sample_count);
                break;
            case DigitalOperationType::Feedback:
            case DigitalOperationType::Mix:
            case DigitalOperationType::Bypass:
                static_cast<void>(next->CopyFrom(*current, sample_count));
                break;
            }
            current = next;
            next = next == &scratch_a_ ? &scratch_b_ : &scratch_a_;
        }
        if (current != &output) {
            static_cast<void>(output.CopyFrom(*current, sample_count));
        }
    }

    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override {
        return declared_latency_samples_;
    }

    [[nodiscard]] bool SetParameter(std::string_view parameter_id, float value) noexcept override {
        const auto previous = parameters_.Replace(parameter_id, value);
        if (!previous.has_value()) {
            return false;
        }
        if (delay_.has_value() && !ConfigureDelay()) {
            parameters_.Restore(parameter_id, *previous);
            return false;
        }
        if (!ConfigureSpecializedOperations()) {
            parameters_.Restore(parameter_id, *previous);
            return false;
        }
        return true;
    }

    [[nodiscard]] std::optional<float> ParameterValue(std::string_view parameter_id) const noexcept override {
        return parameters_.Value(parameter_id);
    }

private:
    [[nodiscard]] bool ConfigureDelay() noexcept {
        if (!delay_.has_value()) {
            return true;
        }
        DigitalDelaySettings settings;
        for (const auto& operation : operations_) {
            switch (operation.type) {
            case DigitalOperationType::Delay:
                settings.delay_seconds = parameters_.Value(operation.parameter_index, settings.delay_seconds);
                break;
            case DigitalOperationType::Feedback:
                settings.feedback = parameters_.Value(operation.parameter_index, settings.feedback);
                break;
            case DigitalOperationType::Mix: {
                const float mix = parameters_.Value(operation.parameter_index, settings.wet);
                settings.wet = mix;
                settings.dry = 1.0F - mix;
                break;
            }
            case DigitalOperationType::Gain:
            case DigitalOperationType::Filter:
            case DigitalOperationType::Waveshaper:
            case DigitalOperationType::Dynamics:
            case DigitalOperationType::PitchShift:
            case DigitalOperationType::Bypass:
                break;
            }
        }
        return delay_->SetSettings(settings);
    }

    [[nodiscard]] bool ConfigureSpecializedOperations() noexcept {
        for (const auto& operation : operations_) {
            const float value = parameters_.Value(operation.parameter_index,
                operation.type == DigitalOperationType::Filter ? 1000.0F : 0.0F);
            switch (operation.type) {
            case DigitalOperationType::Filter:
                if (!filters_[operation.state_index].SetCutoffHz(value)) {
                    return false;
                }
                break;
            case DigitalOperationType::Dynamics:
                if (!dynamics_[operation.state_index].SetAmount(value)) {
                    return false;
                }
                break;
            case DigitalOperationType::PitchShift:
                if (!pitch_shifts_[operation.state_index].SetSemitones(value)) {
                    return false;
                }
                break;
            case DigitalOperationType::Gain:
            case DigitalOperationType::Delay:
            case DigitalOperationType::Feedback:
            case DigitalOperationType::Mix:
            case DigitalOperationType::Waveshaper:
            case DigitalOperationType::Bypass:
                break;
            }
        }
        return true;
    }

    static void ApplyGain(const AudioBuffer& input, AudioBuffer& output, float gain, std::uint32_t sample_count) noexcept {
        for (std::uint32_t channel = 0; channel < input.ChannelCount(); ++channel) {
            const auto* source = input.ChannelData(channel);
            auto* destination = output.ChannelData(channel);
            for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
                destination[sample] = source[sample] * gain;
            }
        }
    }

    static void ApplyWaveshaper(const AudioBuffer& input,
                                AudioBuffer& output,
                                float drive,
                                std::uint32_t sample_count) noexcept {
        for (std::uint32_t channel = 0; channel < input.ChannelCount(); ++channel) {
            const auto* source = input.ChannelData(channel);
            auto* destination = output.ChannelData(channel);
            for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
                destination[sample] = std::tanh(source[sample] * drive);
            }
        }
    }

    std::uint32_t declared_latency_samples_{};
    ParameterState parameters_;
    std::vector<BoundDigitalOperation> operations_;
    std::optional<DigitalDelay> delay_;
    std::vector<DigitalFilter> filters_;
    std::vector<DigitalDynamics> dynamics_;
    std::vector<DigitalPitchShift> pitch_shifts_;
    AudioBuffer scratch_a_;
    AudioBuffer scratch_b_;
    bool prepared_{};
};

class MnaCircuitRuntime final : public ModuleRuntimeProcessor {
public:
    struct Binding {
        std::string parameter_id;
        float minimum{};
        float maximum{};
        std::size_t control_index{};
    };

    static constexpr std::size_t kMaximumBindings = 64U;
    static constexpr std::size_t kMaximumAudioPorts = 8U;

    MnaCircuitRuntime(std::uint32_t declared_latency_samples,
                      MnaPreparedCircuit blueprint,
                      std::vector<Binding> bindings,
                      const std::vector<EffectParameter>& parameters)
        : declared_latency_samples_(declared_latency_samples), blueprint_(std::move(blueprint)),
          bindings_(std::move(bindings)) {
        input_port_count_ = blueprint_.InputCount();
        output_port_count_ = blueprint_.OutputCount();
        for (std::size_t index = 0; index < bindings_.size(); ++index) {
            const auto parameter = std::find_if(parameters.begin(), parameters.end(),
                [this, index](const EffectParameter& item) {
                    return item.id == bindings_[index].parameter_id;
                });
            targets_[index].store(parameter->default_value, std::memory_order_relaxed);
        }
    }

    bool Prepare(const AudioFormat& format) override {
        if (!format.Validate().empty()) {
            return false;
        }
        multi_port_circuit_.reset();
        channel_circuits_.clear();

        if (input_port_count_ != output_port_count_ || input_port_count_ == 0U ||
            input_port_count_ > kMaximumAudioPorts) {
            prepared_ = false;
            return false;
        }
        if (input_port_count_ > 1U) {
            if (format.channel_count != input_port_count_) {
                prepared_ = false;
                return false;
            }
            multi_port_circuit_.emplace(blueprint_);
            if (!multi_port_circuit_->Prepare(format.sample_rate_hz)) {
                multi_port_circuit_.reset();
                prepared_ = false;
                return false;
            }
            prepared_ = true;
            return true;
        }

        channel_circuits_.reserve(format.channel_count);
        for (std::uint32_t channel = 0; channel < format.channel_count; ++channel) {
            channel_circuits_.push_back(blueprint_);
            if (!channel_circuits_.back().Prepare(format.sample_rate_hz)) {
                channel_circuits_.clear();
                prepared_ = false;
                return false;
            }
        }
        prepared_ = !channel_circuits_.empty();
        return prepared_;
    }

    void Process(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept override {
        if (multi_port_circuit_.has_value()) {
            ProcessMultiPort(input, output, sample_count);
            return;
        }
        if (!prepared_ || sample_count > input.Capacity() || sample_count > output.Capacity() ||
            input.ChannelCount() != channel_circuits_.size() || output.ChannelCount() != channel_circuits_.size()) {
            return;
        }
        std::array<float, kMaximumBindings> targets{};
        for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
            for (std::size_t binding = 0; binding < bindings_.size(); ++binding) {
                targets[binding] = targets_[binding].load(std::memory_order_acquire);
            }
            for (std::uint32_t channel = 0; channel < channel_circuits_.size(); ++channel) {
                for (std::size_t binding = 0; binding < bindings_.size(); ++binding) {
                    if (!channel_circuits_[channel].SetControlTarget(
                            bindings_[binding].control_index, targets[binding])) {
                        output.ChannelData(channel)[sample] = 0.0F;
                        continue;
                    }
                }
                const auto* source = input.ChannelData(channel);
                auto* destination = output.ChannelData(channel);
                const auto solved = channel_circuits_[channel].Solve(source[sample]);
                destination[sample] = solved.Succeeded() ? static_cast<float>(solved.output_voltage) : 0.0F;
            }
        }
    }

    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override {
        return declared_latency_samples_;
    }

    [[nodiscard]] bool SetParameter(std::string_view parameter_id, float value) noexcept override {
        if (!std::isfinite(value)) {
            return false;
        }
        for (std::size_t index = 0; index < bindings_.size(); ++index) {
            if (bindings_[index].parameter_id == parameter_id) {
                if (value < bindings_[index].minimum || value > bindings_[index].maximum) {
                    return false;
                }
                targets_[index].store(value, std::memory_order_release);
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::optional<float> ParameterValue(std::string_view parameter_id) const noexcept override {
        for (std::size_t index = 0; index < bindings_.size(); ++index) {
            if (bindings_[index].parameter_id == parameter_id) {
                return targets_[index].load(std::memory_order_acquire);
            }
        }
        return std::nullopt;
    }

private:
    void ProcessMultiPort(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept {
        if (!prepared_ || !multi_port_circuit_.has_value() || sample_count > input.Capacity() ||
            sample_count > output.Capacity() || input.ChannelCount() != input_port_count_ ||
            output.ChannelCount() != output_port_count_) {
            return;
        }

        std::array<float, kMaximumBindings> targets{};
        std::array<double, kMaximumAudioPorts> inputs{};
        for (std::uint32_t sample = 0; sample < sample_count; ++sample) {
            for (std::size_t binding = 0; binding < bindings_.size(); ++binding) {
                targets[binding] = targets_[binding].load(std::memory_order_acquire);
            }
            bool controls_valid = true;
            for (std::size_t binding = 0; binding < bindings_.size(); ++binding) {
                if (!multi_port_circuit_->SetControlTarget(bindings_[binding].control_index, targets[binding])) {
                    controls_valid = false;
                    break;
                }
            }
            if (!controls_valid) {
                for (std::size_t port = 0; port < output_port_count_; ++port) {
                    output.ChannelData(static_cast<std::uint32_t>(port))[sample] = 0.0F;
                }
                continue;
            }
            for (std::size_t port = 0; port < input_port_count_; ++port) {
                inputs[port] = input.ChannelData(static_cast<std::uint32_t>(port))[sample];
            }
            const auto solved = multi_port_circuit_->Solve(
                std::span<const double>(inputs.data(), input_port_count_));
            if (!solved.Succeeded()) {
                for (std::size_t port = 0; port < output_port_count_; ++port) {
                    output.ChannelData(static_cast<std::uint32_t>(port))[sample] = 0.0F;
                }
                continue;
            }
            for (std::size_t port = 0; port < output_port_count_; ++port) {
                const auto value = multi_port_circuit_->OutputValue(port);
                output.ChannelData(static_cast<std::uint32_t>(port))[sample] =
                    value.has_value() && std::isfinite(*value) ? static_cast<float>(*value) : 0.0F;
            }
        }
    }

    std::uint32_t declared_latency_samples_{};
    MnaPreparedCircuit blueprint_;
    std::size_t input_port_count_{};
    std::size_t output_port_count_{};
    std::optional<MnaPreparedCircuit> multi_port_circuit_;
    std::vector<MnaPreparedCircuit> channel_circuits_;
    std::vector<Binding> bindings_;
    std::array<std::atomic<float>, kMaximumBindings> targets_{};
    bool prepared_{};
};

class NeuralAmpModelRuntime final : public ModuleRuntimeProcessor {
public:
    NeuralAmpModelRuntime(std::uint32_t declared_latency_samples, std::unique_ptr<NamProcessor> processor)
        : declared_latency_samples_(declared_latency_samples), processor_(std::move(processor)) {}

    bool Prepare(const AudioFormat& format) override {
        return processor_ != nullptr && processor_->Prepare(format);
    }

    void Process(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept override {
        if (processor_ != nullptr) processor_->Process(input, output, sample_count);
    }

    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override {
        return declared_latency_samples_;
    }

    [[nodiscard]] bool SetParameter(std::string_view, float) noexcept override { return false; }

    [[nodiscard]] std::optional<float> ParameterValue(std::string_view) const noexcept override {
        return std::nullopt;
    }

private:
    std::uint32_t declared_latency_samples_{};
    std::unique_ptr<NamProcessor> processor_;
};

class ImpulseResponseRuntime final : public ModuleRuntimeProcessor {
public:
    ImpulseResponseRuntime(std::uint32_t declared_latency_samples,
                           std::unique_ptr<ImpulseResponseConvolver> convolver)
        : declared_latency_samples_(declared_latency_samples), convolver_(std::move(convolver)) {}

    bool Prepare(const AudioFormat& format) override {
        return convolver_ != nullptr && convolver_->Prepare(format);
    }

    void Process(const AudioBuffer& input, AudioBuffer& output, std::uint32_t sample_count) noexcept override {
        if (convolver_ != nullptr) convolver_->Process(input, output, sample_count);
    }

    [[nodiscard]] std::uint32_t DeclaredLatencySamples() const noexcept override {
        return declared_latency_samples_;
    }

    [[nodiscard]] bool SetParameter(std::string_view, float) noexcept override { return false; }

    [[nodiscard]] std::optional<float> ParameterValue(std::string_view) const noexcept override {
        return std::nullopt;
    }

private:
    std::uint32_t declared_latency_samples_{};
    std::unique_ptr<ImpulseResponseConvolver> convolver_;
};

bool HasSinglePortPair(const EffectDescriptor& descriptor) {
    std::size_t inputs{};
    std::size_t outputs{};
    for (const auto& port : descriptor.ports) {
        if (port.direction == PortDirection::Input) ++inputs;
        if (port.direction == PortDirection::Output) ++outputs;
    }
    return inputs == 1U && outputs == 1U;
}

std::variant<std::unique_ptr<ModuleRuntimeProcessor>, ModuleRuntimeError> CreateNamRuntime(
    const ModulePackage& package, const NeuralAmpModelLogic& logic) {
    if (!HasSinglePortPair(package.descriptor)) {
        return ModuleRuntimeError::UnsupportedPortLayout;
    }
    auto input_port = std::find_if(package.descriptor.ports.begin(), package.descriptor.ports.end(),
        [](const EffectPort& port) { return port.direction == PortDirection::Input; });
    auto output_port = std::find_if(package.descriptor.ports.begin(), package.descriptor.ports.end(),
        [](const EffectPort& port) { return port.direction == PortDirection::Output; });
    auto processor = std::make_unique<NamProcessor>();
    if (processor->LoadModel(logic.model_path).has_value() || !processor->ModelInfo().has_value()) {
        return ModuleRuntimeError::ExternalAssetLoadFailed;
    }
    const auto& info = *processor->ModelInfo();
    if (input_port->max_channels != info.input_channels || output_port->max_channels != info.output_channels ||
        info.input_channels != info.output_channels) {
        return ModuleRuntimeError::UnsupportedPortLayout;
    }
    return std::unique_ptr<ModuleRuntimeProcessor>(std::make_unique<NeuralAmpModelRuntime>(
        package.descriptor.declared_latency_samples, std::move(processor)));
}

std::variant<std::unique_ptr<ModuleRuntimeProcessor>, ModuleRuntimeError> CreateImpulseResponseRuntime(
    const ModulePackage& package, const ImpulseResponseLogic& logic) {
    if (!HasSinglePortPair(package.descriptor)) {
        return ModuleRuntimeError::UnsupportedPortLayout;
    }
    const auto imported = LoadWaveImpulseResponse(logic.impulse_response_path);
    if (!std::holds_alternative<ImportedImpulseResponse>(imported)) {
        return ModuleRuntimeError::ExternalAssetLoadFailed;
    }
    auto convolver = std::make_unique<ImpulseResponseConvolver>();
    if (!convolver->LoadImportedImpulseResponse(std::get<ImportedImpulseResponse>(imported)).empty()) {
        return ModuleRuntimeError::ExternalAssetLoadFailed;
    }
    return std::unique_ptr<ModuleRuntimeProcessor>(std::make_unique<ImpulseResponseRuntime>(
        package.descriptor.declared_latency_samples, std::move(convolver)));
}

std::variant<std::unique_ptr<ModuleRuntimeProcessor>, ModuleRuntimeError> CreateAnalogRuntime(const ModulePackage& package,
                                                                                               const AnalogCircuitLogic& logic) {
    std::optional<float> resistance;
    std::optional<float> capacitance;
    std::optional<std::string> diode_model;
    std::uint32_t diode_count{};
    for (const auto& component : logic.components) {
        switch (component.type) {
        case AnalogComponentType::Resistor:
            if (resistance.has_value() || !component.value.has_value() || !HasNodes(component, "input", "signal")) {
                return ModuleRuntimeError::UnsupportedAnalogTopology;
            }
            resistance = static_cast<float>(*component.value);
            break;
        case AnalogComponentType::Capacitor:
            if (capacitance.has_value() || !component.value.has_value() || !HasNodes(component, "signal", "ground")) {
                return ModuleRuntimeError::UnsupportedAnalogTopology;
            }
            capacitance = static_cast<float>(*component.value);
            break;
        case AnalogComponentType::Diode:
            if (diode_count == 2U || !HasNodes(component, "signal", "ground")) {
                return ModuleRuntimeError::UnsupportedAnalogTopology;
            }
            if (diode_model.has_value() && component.model != diode_model) {
                return ModuleRuntimeError::UnsupportedAnalogTopology;
            }
            diode_model = component.model.value_or("silicon");
            ++diode_count;
            break;
        case AnalogComponentType::Inductor:
        case AnalogComponentType::Potentiometer:
        case AnalogComponentType::OperationalAmplifier:
        case AnalogComponentType::Transistor:
        case AnalogComponentType::VoltageSource:
            return ModuleRuntimeError::UnsupportedAnalogTopology;
        }
    }
    if (!resistance.has_value() || diode_count == 0U) {
        return ModuleRuntimeError::UnsupportedAnalogTopology;
    }

    ParameterState parameters(package.descriptor.parameters);
    DiodeClipperSettings settings{
        .drive = parameters.Value("drive").value_or(1.0F),
        .output_gain = parameters.Value("outputGain").value_or(1.0F),
        .diode_threshold = diode_model == "germanium" ? 0.2F : parameters.Value("diodeThreshold").value_or(0.35F),
        .symmetrical = diode_count == 2U,
        .input_resistance_ohms = *resistance,
        .filter_capacitance_farads = capacitance.value_or(0.0F),
    };
    DiodeClipper validation(settings);
    if (!validation.SetSettings(settings)) {
        return ModuleRuntimeError::InvalidInitialParameterValue;
    }
    return std::unique_ptr<ModuleRuntimeProcessor>(
        std::make_unique<AnalogCircuitRuntime>(package.descriptor.declared_latency_samples, std::move(parameters), settings));
}

std::variant<std::unique_ptr<ModuleRuntimeProcessor>, ModuleRuntimeError> CreateDigitalRuntime(
    const ModulePackage& package,
    const DigitalAlgorithmLogic& logic) {
    ParameterState parameters(package.descriptor.parameters);
    std::vector<BoundDigitalOperation> operations;
    operations.reserve(logic.operations.size());
    std::uint32_t delay_count{};
    bool uses_delay{};
    std::size_t filter_count{};
    std::size_t dynamics_count{};
    std::size_t pitch_shift_count{};
    for (const auto& operation : logic.operations) {
        switch (operation.type) {
        case DigitalOperationType::Gain:
        case DigitalOperationType::Delay:
        case DigitalOperationType::Feedback:
        case DigitalOperationType::Mix:
        case DigitalOperationType::Waveshaper:
        case DigitalOperationType::Bypass:
            break;
        case DigitalOperationType::Filter:
        case DigitalOperationType::Dynamics:
        case DigitalOperationType::PitchShift:
            break;
        }
        if (operation.type == DigitalOperationType::Delay) {
            ++delay_count;
            uses_delay = true;
        }
        std::size_t state_index{};
        switch (operation.type) {
        case DigitalOperationType::Filter:
            state_index = filter_count++;
            break;
        case DigitalOperationType::Dynamics:
            state_index = dynamics_count++;
            break;
        case DigitalOperationType::PitchShift:
            state_index = pitch_shift_count++;
            break;
        case DigitalOperationType::Gain:
        case DigitalOperationType::Delay:
        case DigitalOperationType::Feedback:
        case DigitalOperationType::Mix:
        case DigitalOperationType::Waveshaper:
        case DigitalOperationType::Bypass:
            break;
        }
        operations.push_back({.type = operation.type,
                              .parameter_index = operation.parameter_id.has_value()
                                  ? parameters.Find(*operation.parameter_id)
                                  : std::nullopt,
                              .state_index = state_index});
    }
    if (delay_count > 1U || (!uses_delay && std::ranges::any_of(operations, [](const BoundDigitalOperation& operation) {
            return operation.type == DigitalOperationType::Feedback || operation.type == DigitalOperationType::Mix;
        }))) {
        return ModuleRuntimeError::InvalidDigitalTopology;
    }
    return std::unique_ptr<ModuleRuntimeProcessor>(std::make_unique<DigitalAlgorithmRuntime>(
        package.descriptor.declared_latency_samples, std::move(parameters), std::move(operations), uses_delay,
        filter_count, dynamics_count, pitch_shift_count));
}

std::variant<std::unique_ptr<ModuleRuntimeProcessor>, ModuleRuntimeError> CreateMnaRuntime(
    const ModulePackage& package,
    const CircuitNetlist& circuit,
    const ModelDatabase* model_database = nullptr) {
    const auto compiled = model_database == nullptr
        ? CompileMnaCircuit(circuit) : CompileMnaCircuit(circuit, *model_database);
    if (!compiled.circuit.has_value()) {
        return compiled.error == MnaCompileError::UnsupportedComponent ? ModuleRuntimeError::UnsupportedCircuit
                                                                        : ModuleRuntimeError::UnsupportedAnalogTopology;
    }
    if (compiled.circuit->InputCount() == 0U ||
        compiled.circuit->InputCount() != compiled.circuit->OutputCount() ||
        compiled.circuit->InputCount() > MnaCircuitRuntime::kMaximumAudioPorts) {
        return ModuleRuntimeError::UnsupportedPortLayout;
    }
    if (compiled.circuit->InputCount() > 1U) {
        const auto descriptor_input_count = static_cast<std::size_t>(std::count_if(
            package.descriptor.ports.begin(), package.descriptor.ports.end(),
            [](const EffectPort& port) { return port.direction == PortDirection::Input; }));
        const auto descriptor_output_count = static_cast<std::size_t>(std::count_if(
            package.descriptor.ports.begin(), package.descriptor.ports.end(),
            [](const EffectPort& port) { return port.direction == PortDirection::Output; }));
        if (descriptor_input_count != compiled.circuit->InputCount() ||
            descriptor_output_count != compiled.circuit->OutputCount()) {
            return ModuleRuntimeError::UnsupportedPortLayout;
        }
        for (const auto& circuit_port : circuit.ports) {
            const auto descriptor_port = std::find_if(package.descriptor.ports.begin(), package.descriptor.ports.end(),
                [&circuit_port](const EffectPort& port) {
                    const auto direction = circuit_port.direction == CircuitPortDirection::Input
                        ? PortDirection::Input : PortDirection::Output;
                    return port.id == circuit_port.id && port.direction == direction;
                });
            if (descriptor_port == package.descriptor.ports.end() || descriptor_port->max_channels != 1U) {
                return ModuleRuntimeError::UnsupportedPortLayout;
            }
        }
    }
    if (circuit.parameter_bindings.size() > MnaCircuitRuntime::kMaximumBindings) {
        return ModuleRuntimeError::InvalidInitialParameterValue;
    }
    std::vector<MnaCircuitRuntime::Binding> bindings;
    bindings.reserve(circuit.parameter_bindings.size());
    for (const auto& binding : circuit.parameter_bindings) {
        const auto parameter = std::find_if(package.descriptor.parameters.begin(), package.descriptor.parameters.end(),
            [&binding](const EffectParameter& item) { return item.id == binding.parameter_id; });
        const auto control_index = compiled.circuit->FindControl(binding.parameter_id);
        if (parameter == package.descriptor.parameters.end() || !control_index.has_value()) {
            return ModuleRuntimeError::InvalidInitialParameterValue;
        }
        double target_minimum{};
        double target_maximum{};
        if (binding.property == CircuitParameterBindingProperty::PotentiometerPosition) {
            const auto component = std::find_if(circuit.components.begin(), circuit.components.end(),
                [&binding](const CircuitComponent& item) { return item.id == binding.target_id; });
            if (component == circuit.components.end() || !component->potentiometer.has_value()) {
                return ModuleRuntimeError::InvalidInitialParameterValue;
            }
            target_minimum = component->potentiometer->minimum;
            target_maximum = component->potentiometer->maximum;
        } else if (binding.property == CircuitParameterBindingProperty::SwitchState) {
            target_minimum = 0.0;
            target_maximum = 1.0;
        } else {
            target_minimum = -1.0e6;
            target_maximum = 1.0e6;
        }
        if (!std::isfinite(parameter->minimum) || !std::isfinite(parameter->maximum) ||
            !std::isfinite(parameter->default_value) || parameter->minimum < target_minimum ||
            parameter->maximum > target_maximum || parameter->default_value < parameter->minimum ||
            parameter->default_value > parameter->maximum) {
            return ModuleRuntimeError::InvalidInitialParameterValue;
        }
        bindings.push_back({.parameter_id = binding.parameter_id, .minimum = parameter->minimum,
            .maximum = parameter->maximum, .control_index = *control_index});
    }
    return std::unique_ptr<ModuleRuntimeProcessor>(std::make_unique<MnaCircuitRuntime>(
        package.descriptor.declared_latency_samples, *compiled.circuit, std::move(bindings),
        package.descriptor.parameters));
}

} // namespace

std::variant<std::unique_ptr<ModuleRuntimeProcessor>, ModuleRuntimeError> CreateModuleRuntime(const ModulePackage& package) {
    if (const auto* circuit = std::get_if<AnalogCircuitLogic>(&package.logic)) {
        return package.descriptor.category == EffectCategory::AnalogCircuit ? CreateAnalogRuntime(package, *circuit)
                                                                            : ModuleRuntimeError::LogicCategoryMismatch;
    }
    if (const auto* algorithm = std::get_if<DigitalAlgorithmLogic>(&package.logic)) {
        return package.descriptor.category == EffectCategory::DigitalAlgorithm ? CreateDigitalRuntime(package, *algorithm)
                                                                                : ModuleRuntimeError::LogicCategoryMismatch;
    }
    if (const auto* circuit = std::get_if<CircuitNetlist>(&package.logic)) {
        return package.descriptor.category == EffectCategory::AnalogCircuit ? CreateMnaRuntime(package, *circuit)
                                                                             : ModuleRuntimeError::LogicCategoryMismatch;
    }
    if (const auto* model = std::get_if<NeuralAmpModelLogic>(&package.logic)) {
        return package.descriptor.category == EffectCategory::NeuralAmpModel ? CreateNamRuntime(package, *model)
                                                                               : ModuleRuntimeError::LogicCategoryMismatch;
    }
    if (const auto* impulse = std::get_if<ImpulseResponseLogic>(&package.logic)) {
        return package.descriptor.category == EffectCategory::ImpulseResponse
            ? CreateImpulseResponseRuntime(package, *impulse)
            : ModuleRuntimeError::LogicCategoryMismatch;
    }
    return package.logic.index() == 0U ? ModuleRuntimeError::MissingLogic : ModuleRuntimeError::UnsupportedCategory;
}

std::variant<std::unique_ptr<ModuleRuntimeProcessor>, ModuleRuntimeError> CreateModuleRuntime(
    const ModulePackage& package, const ModelDatabase& model_database) {
    if (const auto* circuit = std::get_if<CircuitNetlist>(&package.logic)) {
        return package.descriptor.category == EffectCategory::AnalogCircuit
            ? CreateMnaRuntime(package, *circuit, &model_database)
            : ModuleRuntimeError::LogicCategoryMismatch;
    }
    return CreateModuleRuntime(package);
}

} // namespace solo_rig
