#include "content/default_preset.hpp"

namespace solo_rig {

RigPreset DefaultRigPreset() {
    return RigPreset{
        .preset_id = "solorig.default-editor",
        .name = "SoloRig Default",
        .nodes = {
            {.instance_id = "drive-1",
             .module_id = "org.mumu-dada.solorig.drive",
             .layout_x = 64.0,
             .layout_y = 148.0,
             .parameter_values = {{"drive", 4.0F}, {"outputGain", 1.0F}, {"diodeThreshold", 0.35F}}},
            {.instance_id = "delay-1",
             .module_id = "org.mumu-dada.solorig.digital-delay",
             .layout_x = 316.0,
             .layout_y = 148.0,
             .parameter_values = {{"time", 0.35F}, {"feedback", 0.35F}, {"mix", 0.25F}}},
        },
        .connections = {{.source_instance_id = "drive-1", .destination_instance_id = "delay-1"}},
    };
}

} // namespace solo_rig
