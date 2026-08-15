#pragma once

#include "pluginterfaces/vst/vsttypes.h"

namespace solo_rig::vst3 {

enum ParameterId : Steinberg::Vst::ParamID {
    kOutputGainId = 1000,
    kBypassId = 1001,
    kDriveId = 1002,
};

// These IDs are persistent plug-in identities. Do not change them after a
// public release because hosts use them to restore saved sessions.
static DECLARE_UID(kProcessorUid, 0x648D109C, 0xBE8C42DA, 0x91F247D1, 0xCA0725A4);
static DECLARE_UID(kControllerUid, 0x6C3AFA17, 0x6D454A9A, 0xB7131E20, 0xD4D9D451);

inline constexpr auto kStateMagic = static_cast<Steinberg::uint32>(0x534C5247U);
inline constexpr auto kStateVersion = static_cast<Steinberg::int32>(3);

} // namespace solo_rig::vst3
