#include "standalone/asio_host.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

struct ProbeContext {
    std::atomic<std::uint64_t> callback_count{};
    std::atomic<std::uint32_t> last_sample_count{};
};

void Passthrough(void* user,
                 const float* const* inputs,
                 float* const* outputs,
                 std::uint32_t channel_count,
                 std::uint32_t sample_count) noexcept {
    auto* context = static_cast<ProbeContext*>(user);
    if (context != nullptr) {
        context->callback_count.fetch_add(1U, std::memory_order_relaxed);
        context->last_sample_count.store(sample_count, std::memory_order_relaxed);
    }
    for (std::uint32_t channel = 0; channel < channel_count; ++channel) {
        if (outputs[channel] == nullptr) {
            continue;
        }
        if (inputs[channel] != nullptr) {
            std::copy_n(inputs[channel], sample_count, outputs[channel]);
        } else {
            std::fill_n(outputs[channel], sample_count, 0.0F);
        }
    }
}

void PrintProbe(const solo_rig::standalone::AsioDriverInfo& driver,
                const solo_rig::standalone::AsioProbeResult& probe) {
    std::cout << driver.driver_id << " | " << driver.display_name;
    if (!probe.succeeded) {
        std::cout << " | probe failed: " << probe.driver_error << '\n';
        return;
    }
    std::cout << " | inputs=" << probe.device.input_channel_count
              << " outputs=" << probe.device.output_channel_count
              << " rate=" << probe.device.native_sample_rate_hz
              << " buffer=" << probe.minimum_buffer_size << ".." << probe.maximum_buffer_size
              << " preferred=" << probe.preferred_buffer_size << '\n';
}

} // namespace

int main(int argc, char** argv) {
    std::string stream_driver;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--stream" && index + 1 < argc) {
            stream_driver = argv[++index];
        }
    }

    const auto drivers = solo_rig::standalone::AsioHost::EnumerateDrivers();
    if (drivers.empty()) {
        std::cout << "No registered ASIO drivers found.\n";
        return stream_driver.empty() ? 0 : 1;
    }
    for (const auto& driver : drivers) {
        const auto probe = solo_rig::standalone::AsioHost::ProbeDriver(driver, GetDesktopWindow());
        PrintProbe(driver, probe);
    }
    if (stream_driver.empty()) {
        return 0;
    }

    const auto selected = std::find_if(drivers.begin(), drivers.end(), [&stream_driver](const auto& driver) {
        return driver.driver_id == stream_driver;
    });
    if (selected == drivers.end()) {
        std::cerr << "Requested ASIO driver was not found: " << stream_driver << '\n';
        return 1;
    }
    const auto probe = solo_rig::standalone::AsioHost::ProbeDriver(*selected, GetDesktopWindow());
    if (!probe.succeeded) {
        std::cerr << "Cannot stream because probe failed: " << probe.driver_error << '\n';
        return 1;
    }
    const auto channel_count = std::min<std::uint32_t>(
        2U,
        std::min(probe.device.input_channel_count, probe.device.output_channel_count));
    if (channel_count == 0U) {
        std::cerr << "Selected ASIO driver has no paired input/output channels.\n";
        return 1;
    }
    const solo_rig::StandaloneAudioRouting routing{
        .processing_format = {.sample_rate_hz = 192000.0,
                              .max_block_size = probe.preferred_buffer_size,
                              .channel_count = channel_count,
                              .sample_representation = solo_rig::SampleRepresentation::Float32},
        .inputs = {},
        .outputs = {},
    };
    auto stream_routing = routing;
    for (std::uint32_t channel = 0; channel < channel_count; ++channel) {
        stream_routing.inputs.push_back({.device_id = selected->driver_id,
                                         .device_channel = channel,
                                         .processing_channel = channel});
        stream_routing.outputs.push_back({.device_id = selected->driver_id,
                                          .device_channel = channel,
                                          .processing_channel = channel});
    }
    solo_rig::standalone::AsioHost host;
    ProbeContext context;
    if (!host.Open(*selected, GetDesktopWindow(), stream_routing, &Passthrough, &context)) {
        std::cerr << "ASIO open failed: " << host.LastErrorMessage() << '\n';
        return 1;
    }
    if (!host.Start()) {
        std::cerr << "ASIO start failed: " << host.LastErrorMessage() << '\n';
        return 1;
    }
    std::cout << "Streaming " << selected->driver_id << " for 1000 ms at 192 kHz.\n";
    Sleep(1000U);
    host.Stop();
    const auto callback_count = context.callback_count.load(std::memory_order_relaxed);
    std::cout << "Stream stopped; callbacks=" << callback_count
              << " samplesPerCallback=" << context.last_sample_count.load(std::memory_order_relaxed)
              << " reset=" << host.ResetRequested()
              << " dropout=" << host.DropoutReported() << '\n';
    return callback_count == 0U ? 1 : 0;
}
