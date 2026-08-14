#pragma once

namespace solo_rig {

enum class NonlinearDeviceKind {
    Invalid,
    Diode,
    Zener,
    Bjt,
    Jfet,
    Mosfet,
    OpAmp,
    Ota,
    Triode,
    Pentode,
    BeamTetrode,
    VariMu,
};

enum class NonlinearDeviceError {
    None,
    InvalidModel,
    NonFinite,
};

} // namespace solo_rig
