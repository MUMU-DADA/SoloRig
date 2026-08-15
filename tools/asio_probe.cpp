#include "standalone/asio_host.hpp"
#include "standalone/standalone_audio_engine.hpp"
#include "ui/rig_editor.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

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
    solo_rig::standalone::StandaloneAudioEngine engine;
    if (!engine.Open(*selected, GetDesktopWindow(), solo_rig::RigEditorViewModel::DefaultEditorPreset())) {
        std::cerr << "Standalone graph ASIO open failed: " << engine.StatusText() << '\n';
        return 1;
    }
    if (!engine.Start()) {
        std::cerr << "Standalone graph ASIO start failed: " << engine.StatusText() << '\n';
        return 1;
    }
    std::cout << "Streaming " << selected->driver_id
              << " through the Standalone drive+delay graph for 1000 ms at 192 kHz.\n";
    Sleep(1000U);
    engine.Stop();
    const auto callback_count = engine.CallbackCount();
    std::cout << "Stream stopped; callbacks=" << callback_count
              << " samplesPerCallback=" << engine.LastCallbackSampleCount()
              << " reset=" << engine.ResetRequested()
              << " dropout=" << engine.DropoutReported() << '\n';
    return callback_count == 0U ? 1 : 0;
}
