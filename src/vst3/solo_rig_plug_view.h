#pragma once

#include "public.sdk/source/common/pluginview.h"

#include <memory>

namespace solo_rig::vst3 {

class ManagedUiBridge;

class SoloRigPlugView final : public Steinberg::CPluginView {
public:
    SoloRigPlugView();
    ~SoloRigPlugView() SMTG_OVERRIDE;

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API removed() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) SMTG_OVERRIDE;

    OBJ_METHODS(SoloRigPlugView, Steinberg::CPluginView)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::IPlugView)
    END_DEFINE_INTERFACES(Steinberg::CPluginView)
    REFCOUNT_METHODS(Steinberg::CPluginView)

private:
    std::unique_ptr<ManagedUiBridge> managed_bridge_;
};

} // namespace solo_rig::vst3
