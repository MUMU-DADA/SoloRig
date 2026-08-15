#pragma once

// This header contains only the small host-side ABI surface required by
// SoloRig.  It is deliberately not a vendored ASIO SDK implementation: the
// ASIO driver supplies the implementation through its registered COM class.

#include <unknwn.h>

#include <cstdint>

namespace solo_rig::standalone::asio_abi {

using ASIOBool = long;
using ASIOError = long;
using ASIOSampleRate = double;
using ASIOSampleType = long;

constexpr ASIOBool kFalse = 0;
constexpr ASIOBool kTrue = 1;
constexpr ASIOError kOk = 0;
constexpr ASIOError kNoClock = -1005;

constexpr ASIOSampleType kFloat32Msb = 3;
constexpr ASIOSampleType kInt16Lsb = 16;
constexpr ASIOSampleType kInt24Lsb = 17;
constexpr ASIOSampleType kInt32Lsb = 18;
constexpr ASIOSampleType kFloat32Lsb = 19;

constexpr long kSelectorSupported = 1;
constexpr long kEngineVersion = 2;
constexpr long kResetRequest = 3;
constexpr long kSupportsTimeInfo = 7;
constexpr long kOverload = 14;

struct ASIOTime;

struct ASIOCallbacks {
    void (*bufferSwitch)(long double_buffer_index, ASIOBool direct_process);
    void (*sampleRateDidChange)(ASIOSampleRate sample_rate);
    long (*asioMessage)(long selector, long value, void* message, double* optional);
    ASIOTime* (*bufferSwitchTimeInfo)(ASIOTime* time_info,
                                      long double_buffer_index,
                                      ASIOBool direct_process);
};

struct ASIOBufferInfo {
    ASIOBool is_input;
    long channel_number;
    void* buffers[2];
};

struct ASIOChannelInfo {
    long channel;
    ASIOBool is_input;
    ASIOBool is_active;
    long channel_group;
    ASIOSampleType type;
    char name[32];
};

// ASIO drivers expose this COM-compatible interface.  The driver class GUID
// is also used as the requested interface ID by the ASIO host convention.
struct IASIO : IUnknown {
    virtual ASIOBool init(void* system_handle) = 0;
    virtual void getDriverName(char* name) = 0;
    virtual long getDriverVersion() = 0;
    virtual void getErrorMessage(char* message) = 0;
    virtual ASIOError start() = 0;
    virtual ASIOError stop() = 0;
    virtual ASIOError getChannels(long* input_channels, long* output_channels) = 0;
    virtual ASIOError getLatencies(long* input_latency, long* output_latency) = 0;
    virtual ASIOError getBufferSize(long* minimum_size,
                                    long* maximum_size,
                                    long* preferred_size,
                                    long* granularity) = 0;
    virtual ASIOError canSampleRate(ASIOSampleRate sample_rate) = 0;
    virtual ASIOError getSampleRate(ASIOSampleRate* sample_rate) = 0;
    virtual ASIOError setSampleRate(ASIOSampleRate sample_rate) = 0;
    virtual ASIOError getClockSources(void* clocks, long* source_count) = 0;
    virtual ASIOError setClockSource(long reference) = 0;
    virtual ASIOError getSamplePosition(void* sample_position, void* time_stamp) = 0;
    virtual ASIOError getChannelInfo(ASIOChannelInfo* info) = 0;
    virtual ASIOError createBuffers(ASIOBufferInfo* buffer_infos,
                                    long channel_count,
                                    long buffer_size,
                                    ASIOCallbacks* callbacks) = 0;
    virtual ASIOError disposeBuffers() = 0;
    virtual ASIOError controlPanel() = 0;
    virtual ASIOError future(long selector, void* optional) = 0;
    virtual ASIOError outputReady() = 0;
};

static_assert(sizeof(ASIOBufferInfo) == sizeof(long) * 2U + sizeof(void*) * 2U);
static_assert(sizeof(ASIOChannelInfo) == sizeof(long) * 4U + sizeof(ASIOSampleType) + 32U);

} // namespace solo_rig::standalone::asio_abi
