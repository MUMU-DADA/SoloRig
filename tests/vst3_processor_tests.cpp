#include "test_framework.hpp"

#include "vst3/solo_rig_processor.h"
#include "vst3/solo_rig_ids.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

#include <array>
#include <atomic>
#include <cmath>
#include <thread>

namespace {

using solo_rig::vst3::SoloRigProcessor;
using Steinberg::Vst::AudioBusBuffers;

void Setup(SoloRigProcessor& processor,
           Steinberg::int32 max_samples,
           Steinberg::Vst::SampleRate sample_rate) {
    Steinberg::Vst::ProcessSetup setup{
        .processMode = Steinberg::Vst::kRealtime,
        .symbolicSampleSize = Steinberg::Vst::kSample32,
        .maxSamplesPerBlock = max_samples,
        .sampleRate = sample_rate,
    };
    REQUIRE(processor.setupProcessing(setup) == Steinberg::kResultOk);
}

void Configure(SoloRigProcessor& processor,
               Steinberg::int32 max_samples = 64,
               Steinberg::Vst::SampleRate sample_rate = 48000.0) {
    REQUIRE(processor.initialize(nullptr) == Steinberg::kResultOk);
    Steinberg::Vst::SpeakerArrangement arrangement = Steinberg::Vst::SpeakerArr::kStereo;
    REQUIRE(processor.setBusArrangements(&arrangement, 1, &arrangement, 1) == Steinberg::kResultOk);
    Setup(processor, max_samples, sample_rate);
}

void StateStream(Steinberg::MemoryStream& stream,
                 float output_gain,
                 Steinberg::int32 bypass,
                 float drive,
                 Steinberg::int32 version = 2) {
    Steinberg::IBStreamer writer(&stream, kLittleEndian);
    REQUIRE(writer.writeInt32u(solo_rig::vst3::kStateMagic));
    REQUIRE(writer.writeInt32(version));
    REQUIRE(writer.writeFloat(output_gain));
    REQUIRE(writer.writeInt32(bypass));
    if (version >= 2) {
        REQUIRE(writer.writeFloat(drive));
    }
    Steinberg::int64 position = 0;
    REQUIRE(stream.seek(0, Steinberg::IBStream::kIBSeekSet, &position) == Steinberg::kResultTrue);
}

std::array<float, 8> ProcessBlock(SoloRigProcessor& processor,
                                  float value = 0.25F,
                                  Steinberg::Vst::IParameterChanges* changes = nullptr) {
    std::array<float, 8> input{};
    std::array<float, 8> output{};
    input.fill(value);
    float* input_channels[] = {input.data(), input.data()};
    float* output_channels[] = {output.data(), output.data()};
    AudioBusBuffers input_bus{};
    input_bus.numChannels = 2;
    input_bus.channelBuffers32 = input_channels;
    AudioBusBuffers output_bus{};
    output_bus.numChannels = 2;
    output_bus.channelBuffers32 = output_channels;
    Steinberg::Vst::ProcessData data{};
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = static_cast<Steinberg::int32>(input.size());
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &input_bus;
    data.outputs = &output_bus;
    data.inputParameterChanges = changes;
    REQUIRE(processor.process(data) == Steinberg::kResultOk);
    return output;
}

void ProcessMonoBlock(SoloRigProcessor& processor) {
    std::array<float, 8> input{};
    std::array<float, 8> output{};
    input.fill(0.25F);
    float* input_channels[] = {input.data()};
    float* output_channels[] = {output.data()};
    AudioBusBuffers input_bus{};
    input_bus.numChannels = 1;
    input_bus.channelBuffers32 = input_channels;
    AudioBusBuffers output_bus{};
    output_bus.numChannels = 1;
    output_bus.channelBuffers32 = output_channels;
    Steinberg::Vst::ProcessData data{};
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = static_cast<Steinberg::int32>(input.size());
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &input_bus;
    data.outputs = &output_bus;
    REQUIRE(processor.process(data) == Steinberg::kResultOk);
    for (const auto sample : output) {
        REQUIRE(std::isfinite(sample));
    }
}

} // namespace

TEST_CASE("VST3 state v1 restores before setup and keeps legacy defaults") {
    SoloRigProcessor legacy;
    Steinberg::MemoryStream state;
    StateStream(state, 0.5F, 0, 0.0F, 1);
    REQUIRE(legacy.setState(&state) == Steinberg::kResultOk);
    Configure(legacy);
    const auto output = ProcessBlock(legacy);
    for (const auto sample : output) {
        REQUIRE(std::isfinite(sample));
    }
}

TEST_CASE("VST3 v2 state before and after setup has equivalent Drive output") {
    SoloRigProcessor before;
    Steinberg::MemoryStream before_state;
    StateStream(before_state, 0.5F, 0, 0.6F);
    REQUIRE(before.setState(&before_state) == Steinberg::kResultOk);
    Configure(before);
    const auto expected = ProcessBlock(before);

    SoloRigProcessor after;
    Configure(after);
    Steinberg::MemoryStream after_state;
    StateStream(after_state, 0.5F, 0, 0.6F);
    REQUIRE(after.setState(&after_state) == Steinberg::kResultOk);
    const auto actual = ProcessBlock(after);
    for (std::size_t index = 0; index < expected.size(); ++index) {
        REQUIRE(std::fabs(expected[index] - actual[index]) < 1.0e-6F);
    }
}

TEST_CASE("VST3 Bypass preserves existing gain bypass semantics") {
    SoloRigProcessor processor;
    Steinberg::MemoryStream state;
    StateStream(state, 0.0F, 1, 1.0F);
    REQUIRE(processor.setState(&state) == Steinberg::kResultOk);
    Configure(processor);
    const auto output = ProcessBlock(processor, 0.37F);
    for (const auto sample : output) {
        REQUIRE(std::fabs(sample - 0.37F) < 1.0e-6F);
    }
}

TEST_CASE("VST3 repeated setupProcessing releases pending and transition runtime") {
    SoloRigProcessor processor;
    Configure(processor, 16, 48000.0);
    Steinberg::MemoryStream state;
    StateStream(state, 0.5F, 0, 0.3F);
    REQUIRE(processor.setState(&state) == Steinberg::kResultOk);
    static_cast<void>(ProcessBlock(processor));
    Setup(processor, 32, 96000.0);
    Steinberg::Vst::SpeakerArrangement mono = Steinberg::Vst::SpeakerArr::kMono;
    REQUIRE(processor.setBusArrangements(&mono, 1, &mono, 1) == Steinberg::kResultOk);
    Setup(processor, 8, 44100.0);
    ProcessMonoBlock(processor);
    Steinberg::Vst::SpeakerArrangement stereo = Steinberg::Vst::SpeakerArr::kStereo;
    REQUIRE(processor.setBusArrangements(&stereo, 1, &stereo, 1) == Steinberg::kResultOk);
    Setup(processor, 8, 44100.0);
    const auto output = ProcessBlock(processor);
    for (const auto sample : output) {
        REQUIRE(std::isfinite(sample));
    }
}

TEST_CASE("VST3 automation retains the last point in a block") {
    SoloRigProcessor automated;
    Configure(automated);
    Steinberg::Vst::ParameterChanges changes(1);
    Steinberg::int32 queue_index = 0;
    auto* queue = changes.addParameterData(solo_rig::vst3::kDriveId, queue_index);
    REQUIRE(queue != nullptr);
    Steinberg::int32 point_index = 0;
    REQUIRE(queue->addPoint(0, 1.0, point_index) == Steinberg::kResultOk);
    REQUIRE(queue->addPoint(4, 0.0, point_index) == Steinberg::kResultOk);
    const auto automated_output = ProcessBlock(automated, 0.25F, &changes);

    SoloRigProcessor baseline;
    Configure(baseline);
    const auto baseline_output = ProcessBlock(baseline, 0.25F);
    for (std::size_t index = 0; index < baseline_output.size(); ++index) {
        REQUIRE(std::fabs(automated_output[index] - baseline_output[index]) < 1.0e-6F);
    }
}

TEST_CASE("VST3 state publication is race-safe while processing continues") {
    SoloRigProcessor processor;
    Configure(processor);
    std::atomic<bool> finished{false};
    std::atomic<bool> writer_ok{true};
    std::thread state_writer([&] {
        for (int index = 0; index < 100; ++index) {
            Steinberg::MemoryStream state;
            StateStream(state, index % 2 == 0 ? 0.5F : 0.75F, index % 2, index / 100.0F);
            if (processor.setState(&state) != Steinberg::kResultOk) {
                writer_ok.store(false, std::memory_order_release);
            }
        }
        finished.store(true, std::memory_order_release);
    });
    for (int index = 0; index < 100; ++index) {
        const auto output = ProcessBlock(processor);
        for (const auto sample : output) {
            REQUIRE(std::isfinite(sample));
        }
    }
    state_writer.join();
    REQUIRE(finished.load(std::memory_order_acquire));
    REQUIRE(writer_ok.load(std::memory_order_acquire));

    Steinberg::MemoryStream final_state;
    StateStream(final_state, 0.5F, 0, 0.8F);
    REQUIRE(processor.setState(&final_state) == Steinberg::kResultOk);
    const auto final_output = ProcessBlock(processor);
    SoloRigProcessor expected;
    Steinberg::MemoryStream expected_state;
    StateStream(expected_state, 0.5F, 0, 0.8F);
    REQUIRE(expected.setState(&expected_state) == Steinberg::kResultOk);
    Configure(expected);
    const auto expected_output = ProcessBlock(expected);
    for (std::size_t index = 0; index < final_output.size(); ++index) {
        REQUIRE(std::fabs(final_output[index] - expected_output[index]) < 1.0e-6F);
    }
}
