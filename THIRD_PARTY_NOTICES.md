# Third-party notices

## NeuralAmpModelerCore

`third_party/NeuralAmpModelerCore` is vendored from NeuralAmpModelerCore tag
`v0.5.2` (commit `ab72c078cd141be60d963d357bd501d56cc9606b`) for real-time
NAM A1 and A2 model execution. It is licensed under the MIT License.

Copyright (c) 2023 Steven Atkinson

The full license text is retained in
`third_party/NeuralAmpModelerCore/LICENSE`.

## Eigen

The vendored Eigen headers required by NeuralAmpModelerCore are primarily
licensed under Mozilla Public License 2.0. The applicable license and
third-party notices are retained in
`third_party/NeuralAmpModelerCore/Dependencies/eigen/COPYING.*`.

## JSON for Modern C++

The single-header nlohmann JSON dependency required by NeuralAmpModelerCore is
MIT licensed. Its SPDX copyright and license declaration are retained in
`third_party/NeuralAmpModelerCore/Dependencies/nlohmann/json.hpp`.

## VST 3 SDK

`third_party/vst3sdk` is vendored from the official Steinberg VST 3 SDK for
building and validating the SoloRig VST3 plug-in. Its source is licensed under
the MIT License; the complete license text is retained in
`third_party/vst3sdk/LICENSE.txt`.

Copyright (c) 2026 Steinberg Media Technologies GmbH.

## ASIO host boundary

SoloRig does not vendor or redistribute an ASIO SDK implementation. The
Windows standalone host contains only the small ABI declarations needed to
call an already-installed, user-selected ASIO driver through its registered
COM class. Any distribution that bundles an ASIO SDK or driver must complete
the applicable Steinberg licensing and driver-vendor permission review before
shipping.
