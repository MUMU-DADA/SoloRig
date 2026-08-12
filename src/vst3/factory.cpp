#include "vst3/solo_rig_controller.h"
#include "vst3/solo_rig_ids.h"
#include "vst3/solo_rig_processor.h"

#include "public.sdk/source/main/pluginfactory_constexpr.h"

#define SOLO_RIG_PLUGIN_NAME "SoloRig"
#define SOLO_RIG_PLUGIN_VERSION "0.1.0"

BEGIN_FACTORY_DEF("MUMU-DADA", "", "", 2)

DEF_CLASS(
    solo_rig::vst3::kProcessorUid,
    Steinberg::PClassInfo::kManyInstances,
    kVstAudioEffectClass,
    SOLO_RIG_PLUGIN_NAME,
    Steinberg::Vst::kDistributable,
    "Fx|Guitar",
    SOLO_RIG_PLUGIN_VERSION,
    kVstVersionString,
    solo_rig::vst3::SoloRigProcessor::createInstance,
    nullptr)

DEF_CLASS(
    solo_rig::vst3::kControllerUid,
    Steinberg::PClassInfo::kManyInstances,
    kVstComponentControllerClass,
    SOLO_RIG_PLUGIN_NAME " Controller",
    0,
    "",
    SOLO_RIG_PLUGIN_VERSION,
    kVstVersionString,
    solo_rig::vst3::SoloRigController::createInstance,
    nullptr)

END_FACTORY
