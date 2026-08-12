#include "standalone/audio_routing.hpp"

#include <cmath>
#include <map>
#include <set>

namespace solo_rig {
namespace {

bool IsValidDeviceDescriptor(const AsioDeviceDescriptor& device) {
    return !device.device_id.empty() && !device.display_name.empty() &&
        std::isfinite(device.native_sample_rate_hz) && device.native_sample_rate_hz > 0.0;
}

std::string PhysicalChannelKey(const PhysicalChannelRoute& route) {
    return route.device_id + ':' + std::to_string(route.device_channel);
}

} // namespace

AudioRoutingValidationResult ValidateStandaloneAudioRouting(
    const std::vector<AsioDeviceDescriptor>& devices,
    const StandaloneAudioRouting& routing) {
    AudioRoutingValidationResult result;
    if (!routing.processing_format.Validate().empty()) {
        result.errors.insert(AudioRoutingError::InvalidProcessingFormat);
    }
    if (std::fabs(routing.processing_format.sample_rate_hz - 192000.0) > 0.001) {
        result.errors.insert(AudioRoutingError::InternalSampleRateMustBe192k);
    }
    if (routing.inputs.empty()) {
        result.errors.insert(AudioRoutingError::MissingInputRoute);
    }
    if (routing.outputs.empty()) {
        result.errors.insert(AudioRoutingError::MissingOutputRoute);
    }

    std::map<std::string, const AsioDeviceDescriptor*, std::less<>> device_by_id;
    for (const auto& device : devices) {
        if (!IsValidDeviceDescriptor(device)) {
            result.errors.insert(AudioRoutingError::InvalidDeviceDescriptor);
            continue;
        }
        if (!device_by_id.emplace(device.device_id, &device).second) {
            result.errors.insert(AudioRoutingError::DuplicateDeviceId);
        }
    }

    std::set<std::string> active_device_ids;
    std::set<std::string> input_physical_channels;
    std::set<std::uint32_t> input_processing_channels;
    std::set<std::string> output_physical_channels;
    std::set<std::string> active_clock_domains;
    const auto validate_processing_channel = [&result, &routing](std::uint32_t processing_channel) {
        if (processing_channel >= routing.processing_format.channel_count) {
            result.errors.insert(AudioRoutingError::ProcessingChannelOutOfRange);
            return false;
        }
        return true;
    };

    for (const auto& route : routing.inputs) {
        const auto device = device_by_id.find(route.device_id);
        if (device == device_by_id.end()) {
            result.errors.insert(AudioRoutingError::UnknownInputDevice);
            continue;
        }
        if (route.device_channel >= device->second->input_channel_count) {
            result.errors.insert(AudioRoutingError::InputChannelOutOfRange);
        }
        static_cast<void>(validate_processing_channel(route.processing_channel));
        if (!input_physical_channels.insert(PhysicalChannelKey(route)).second) {
            result.errors.insert(AudioRoutingError::DuplicateInputPhysicalChannel);
        }
        if (!input_processing_channels.insert(route.processing_channel).second) {
            result.errors.insert(AudioRoutingError::DuplicateInputProcessingChannel);
        }
        active_device_ids.insert(device->first);
        active_clock_domains.insert(device->second->clock_domain_id.empty() ? device->first : device->second->clock_domain_id);
    }

    for (const auto& route : routing.outputs) {
        const auto device = device_by_id.find(route.device_id);
        if (device == device_by_id.end()) {
            result.errors.insert(AudioRoutingError::UnknownOutputDevice);
            continue;
        }
        if (route.device_channel >= device->second->output_channel_count) {
            result.errors.insert(AudioRoutingError::OutputChannelOutOfRange);
        }
        static_cast<void>(validate_processing_channel(route.processing_channel));
        if (!output_physical_channels.insert(PhysicalChannelKey(route)).second) {
            result.errors.insert(AudioRoutingError::DuplicateOutputPhysicalChannel);
        }
        active_device_ids.insert(device->first);
        active_clock_domains.insert(device->second->clock_domain_id.empty() ? device->first : device->second->clock_domain_id);
    }

    result.analysis.active_device_count = static_cast<std::uint32_t>(active_device_ids.size());
    result.analysis.requires_asynchronous_resampling = active_clock_domains.size() > 1U;
    for (const auto& device_id : active_device_ids) {
        const auto* device = device_by_id.at(device_id);
        if (std::fabs(device->native_sample_rate_hz - routing.processing_format.sample_rate_hz) > 0.001) {
            ++result.analysis.device_rate_conversion_count;
        }
    }
    return result;
}

} // namespace solo_rig
