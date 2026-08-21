# Third-party notices

PoseAnchor downloads exact, immutable revisions during configuration:

- ValveSoftware/OpenVR, commit `0924064316de3effbcd1acf1e309182a2deb1c05`
  (OpenVR v2.15.6), BSD-3-Clause.
- TsudaKageyu/MinHook, commit `c3fcafdc10146beb5919319d0683e44e3c30d537`
  (MinHook v1.3.4), BSD-2-Clause. MinHook's license also contains the notices
  for Hacker Disassembler Engine 32/64.

The build copies the complete upstream license texts to `licenses/` in the
driver package.

The graphical Windows installer is built with Inno Setup:

- JRSoftware/Inno Setup 6, Copyright (c) 1997-2026 Jordan Russell and
  Copyright (c) 2000-2026 Martijn Laan, Inno Setup License.

The complete Inno Setup license is included as
`licenses/Inno-Setup-LICENSE.txt`. Inno Setup is a build-time dependency; its
installer runtime is embedded only in the generated Setup and uninstaller.

The SteamVR interception architecture was informed by OpenVR Space Calibrator:

- `a-popp/openvr-spacecalibrator` and its maintained successor
  `hyblocker/OpenVR-SpaceCalibrator`, MIT License.
- Copyright (c) 2023-2026 Hyblocker and other contributors.
- Copyright (c) 2020-2022 Justin Li and other contributors.

No OpenVR Space Calibrator source file is vendored. Its public MIT-licensed
driver pattern (hooking `IVRDriverContext::GetGenericInterface` and
`IVRServerDriverHost::TrackedDevicePoseUpdated`) is credited here. The complete
MIT notice is included as `licenses/OpenVR-SpaceCalibrator-LICENSE.txt`.
