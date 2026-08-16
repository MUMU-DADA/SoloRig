#include "test_framework.hpp"

#include "vst3/solo_rig_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/common/memorystream.h"

#include <array>
#include <cmath>
#include <windows.h>

extern "C" Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory();

namespace {

struct HostInstance {
    Steinberg::Vst::IAudioProcessor* processor{};
    Steinberg::Vst::IComponent* component{};

    HostInstance() = default;
    HostInstance(const HostInstance&) = delete;
    HostInstance& operator=(const HostInstance&) = delete;
    HostInstance(HostInstance&& other) noexcept
        : processor(other.processor), component(other.component) {
        other.processor = nullptr;
        other.component = nullptr;
    }
    HostInstance& operator=(HostInstance&& other) noexcept {
        if (this != &other) {
            if (component != nullptr) {
                static_cast<void>(component->terminate());
                component->release();
            }
            if (processor != nullptr) {
                processor->release();
            }
            processor = other.processor;
            component = other.component;
            other.processor = nullptr;
            other.component = nullptr;
        }
        return *this;
    }

    ~HostInstance() {
        if (component != nullptr) {
            static_cast<void>(component->terminate());
            component->release();
        }
        if (processor != nullptr) {
            processor->release();
        }
    }
};

HostInstance CreateInstance() {
    auto* factory = GetPluginFactory();
    REQUIRE(factory != nullptr);
    REQUIRE(factory->countClasses() == 2);

    HostInstance instance;
    REQUIRE(factory->createInstance(solo_rig::vst3::kProcessorUid,
                                    Steinberg::Vst::IAudioProcessor::iid,
                                    reinterpret_cast<void**>(&instance.processor)) == Steinberg::kResultOk);
    REQUIRE(instance.processor != nullptr);
    REQUIRE(instance.processor->queryInterface(Steinberg::Vst::IComponent::iid,
                                               reinterpret_cast<void**>(&instance.component)) == Steinberg::kResultOk);
    REQUIRE(instance.component != nullptr);
    REQUIRE(instance.component->initialize(nullptr) == Steinberg::kResultOk);
    return instance;
}

void Setup(HostInstance& instance, Steinberg::int32 max_samples, double sample_rate) {
    auto arrangement = Steinberg::Vst::SpeakerArr::kStereo;
    REQUIRE(instance.processor->setBusArrangements(&arrangement, 1, &arrangement, 1) == Steinberg::kResultOk);
    Steinberg::Vst::ProcessSetup setup{
        .processMode = Steinberg::Vst::kRealtime,
        .symbolicSampleSize = Steinberg::Vst::kSample32,
        .maxSamplesPerBlock = max_samples,
        .sampleRate = sample_rate,
    };
    REQUIRE(instance.processor->setupProcessing(setup) == Steinberg::kResultOk);
}

std::array<float, 16> Process(HostInstance& instance, float input_value) {
    std::array<float, 16> input{};
    std::array<float, 16> output{};
    input.fill(input_value);
    float* input_channels[] = {input.data(), input.data()};
    float* output_channels[] = {output.data(), output.data()};
    Steinberg::Vst::AudioBusBuffers input_bus{};
    input_bus.numChannels = 2;
    input_bus.channelBuffers32 = input_channels;
    Steinberg::Vst::AudioBusBuffers output_bus{};
    output_bus.numChannels = 2;
    output_bus.channelBuffers32 = output_channels;
    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = static_cast<Steinberg::int32>(input.size());
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &input_bus;
    data.outputs = &output_bus;
    REQUIRE(instance.processor->process(data) == Steinberg::kResultOk);
    return output;
}

} // namespace

TEST_CASE("VST3 exported factory supports host component lifecycle and state restore") {
    auto first = CreateInstance();
    Setup(first, 32, 48000.0);
    const auto first_output = Process(first, 0.25F);
    for (const auto sample : first_output) {
        REQUIRE(std::isfinite(sample));
    }

    Steinberg::MemoryStream state;
    REQUIRE(first.component->getState(&state) == Steinberg::kResultOk);
    Steinberg::int64 position = 0;
    REQUIRE(state.seek(0, Steinberg::IBStream::kIBSeekSet, &position) == Steinberg::kResultTrue);

    auto restored = CreateInstance();
    REQUIRE(restored.component->setState(&state) == Steinberg::kResultOk);
    Setup(restored, 7, 96000.0);
    const auto restored_output = Process(restored, 0.25F);
    for (const auto sample : restored_output) {
        REQUIRE(std::isfinite(sample));
    }
}

TEST_CASE("VST3 controller exposes the shared C# Avalonia IPlugView") {
    auto* factory = GetPluginFactory();
    REQUIRE(factory != nullptr);

    Steinberg::Vst::IEditController* controller{};
    REQUIRE(factory->createInstance(solo_rig::vst3::kControllerUid,
                                    Steinberg::Vst::IEditController::iid,
                                    reinterpret_cast<void**>(&controller)) == Steinberg::kResultOk);
    REQUIRE(controller != nullptr);
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);

    auto* view = controller->createView(Steinberg::Vst::ViewType::kEditor);
    REQUIRE(view != nullptr);
    REQUIRE(view->isPlatformTypeSupported(Steinberg::kPlatformTypeHWND) == Steinberg::kResultTrue);
    REQUIRE(view->isPlatformTypeSupported(Steinberg::kPlatformTypeX11EmbedWindowID) ==
            Steinberg::kResultFalse);

    const auto parent = CreateWindowExW(0, L"STATIC", L"SoloRig VST3 C# UI host probe",
                                        WS_OVERLAPPEDWINDOW, 0, 0, 1024, 720,
                                        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    REQUIRE(parent != nullptr);
    REQUIRE(view->attached(parent, Steinberg::kPlatformTypeHWND) == Steinberg::kResultOk);

    Steinberg::ViewRect size{};
    REQUIRE(view->getSize(&size) == Steinberg::kResultTrue);
    REQUIRE(size.getWidth() >= 320);
    REQUIRE(size.getHeight() >= 220);

    Steinberg::ViewRect resized(0, 0, 720, 480);
    REQUIRE(view->onSize(&resized) == Steinberg::kResultTrue);
    REQUIRE(view->removed() == Steinberg::kResultOk);
    view->release();
    controller->terminate();
    controller->release();
    DestroyWindow(parent);
}
