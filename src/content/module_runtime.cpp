#include "content/module_runtime.hpp"

#include "dsp/digital_delay.hpp"
#include "dsp/diode_clipper.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
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
};

class DigitalAlgorithmRuntime final : public ModuleRuntimeProcessor {
public:
    DigitalAlgorithmRuntime(std::uint32_t declared_latency_samples,
                            ParameterState parameters,
                            std::vector<BoundDigitalOperation> operations,
                            bool uses_delay)
        : declared_latency_samples_(declared_latency_samples), parameters_(std::move(parameters)), operations_(std::move(operations)) {
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
            case DigitalOperationType::Delay:
                delay_->Process(*current, *next, sample_count);
                break;
            case DigitalOperationType::Feedback:
            case DigitalOperationType::Mix:
            case DigitalOperationType::Bypass:
                static_cast<void>(next->CopyFrom(*current, sample_count));
                break;
            case DigitalOperationType::Filter:
            case DigitalOperationType::Dynamics:
            case DigitalOperationType::PitchShift:
                return;
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
    AudioBuffer scratch_a_;
    AudioBuffer scratch_b_;
    bool prepared_{};
};

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
            return ModuleRuntimeError::UnsupportedDigitalOperation;
        }
        if (operation.type == DigitalOperationType::Delay) {
            ++delay_count;
            uses_delay = true;
        }
        operations.push_back({.type = operation.type,
                              .parameter_index = operation.parameter_id.has_value()
                                  ? parameters.Find(*operation.parameter_id)
                                  : std::nullopt});
    }
    if (delay_count > 1U || (!uses_delay && std::ranges::any_of(operations, [](const BoundDigitalOperation& operation) {
            return operation.type == DigitalOperationType::Feedback || operation.type == DigitalOperationType::Mix;
        }))) {
        return ModuleRuntimeError::InvalidDigitalTopology;
    }
    return std::unique_ptr<ModuleRuntimeProcessor>(std::make_unique<DigitalAlgorithmRuntime>(
        package.descriptor.declared_latency_samples, std::move(parameters), std::move(operations), uses_delay));
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
    return package.logic.index() == 0U ? ModuleRuntimeError::MissingLogic : ModuleRuntimeError::UnsupportedCategory;
}

} // namespace solo_rig
