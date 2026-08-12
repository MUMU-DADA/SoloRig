#include "standalone/audio_routing.hpp"
#include "test_framework.hpp"

namespace {

solo_rig::AudioFormat CreateInternalFormat() {
    return {.sample_rate_hz = 192000.0, .max_block_size = 256, .channel_count = 2};
}

} // namespace

TEST_CASE("Standalone ASIO routing accepts arbitrary physical channels in one clock domain") {
    const std::vector<solo_rig::AsioDeviceDescriptor> devices{
        {.device_id = "usb-interface", .display_name = "USB Interface", .input_channel_count = 4,
         .output_channel_count = 6, .native_sample_rate_hz = 48000.0, .clock_domain_id = "usb-clock"},
    };
    const solo_rig::StandaloneAudioRouting routing{
        .processing_format = CreateInternalFormat(),
        .inputs = {{.device_id = "usb-interface", .device_channel = 2, .processing_channel = 0},
                   {.device_id = "usb-interface", .device_channel = 3, .processing_channel = 1}},
        .outputs = {{.device_id = "usb-interface", .device_channel = 4, .processing_channel = 0},
                    {.device_id = "usb-interface", .device_channel = 5, .processing_channel = 1}},
    };

    const auto result = solo_rig::ValidateStandaloneAudioRouting(devices, routing);

    REQUIRE(result.errors.empty());
    REQUIRE(!result.analysis.requires_asynchronous_resampling);
    REQUIRE(result.analysis.active_device_count == 1U);
}

TEST_CASE("Standalone ASIO routing marks independent device clocks for asynchronous resampling") {
    const std::vector<solo_rig::AsioDeviceDescriptor> devices{
        {.device_id = "input-interface", .display_name = "Input", .input_channel_count = 2,
         .output_channel_count = 0, .native_sample_rate_hz = 44100.0, .clock_domain_id = "input-clock"},
        {.device_id = "output-interface", .display_name = "Output", .input_channel_count = 0,
         .output_channel_count = 2, .native_sample_rate_hz = 48000.0, .clock_domain_id = "output-clock"},
    };
    const solo_rig::StandaloneAudioRouting routing{
        .processing_format = CreateInternalFormat(),
        .inputs = {{.device_id = "input-interface", .device_channel = 0, .processing_channel = 0}},
        .outputs = {{.device_id = "output-interface", .device_channel = 1, .processing_channel = 0}},
    };

    const auto result = solo_rig::ValidateStandaloneAudioRouting(devices, routing);

    REQUIRE(result.errors.empty());
    REQUIRE(result.analysis.active_device_count == 2U);
    REQUIRE(result.analysis.requires_asynchronous_resampling);
    REQUIRE(result.analysis.device_rate_conversion_count == 2U);
}

TEST_CASE("Standalone ASIO routing rejects unsupported formats and invalid channel maps") {
    const std::vector<solo_rig::AsioDeviceDescriptor> devices{
        {.device_id = "interface", .display_name = "Interface", .input_channel_count = 1,
         .output_channel_count = 1, .native_sample_rate_hz = 48000.0, .clock_domain_id = "clock"},
    };
    const solo_rig::StandaloneAudioRouting routing{
        .processing_format = {.sample_rate_hz = 48000.0, .max_block_size = 128, .channel_count = 2},
        .inputs = {{.device_id = "interface", .device_channel = 1, .processing_channel = 2}},
        .outputs = {{.device_id = "missing", .device_channel = 0, .processing_channel = 0}},
    };

    const auto result = solo_rig::ValidateStandaloneAudioRouting(devices, routing);

    REQUIRE(!result.errors.empty());
    REQUIRE(result.errors.contains(solo_rig::AudioRoutingError::InternalSampleRateMustBe192k));
    REQUIRE(result.errors.contains(solo_rig::AudioRoutingError::InputChannelOutOfRange));
    REQUIRE(result.errors.contains(solo_rig::AudioRoutingError::ProcessingChannelOutOfRange));
    REQUIRE(result.errors.contains(solo_rig::AudioRoutingError::UnknownOutputDevice));
}
