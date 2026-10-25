# Third-party notices

PoseAnchor downloads exact, immutable revisions during configuration:

- ValveSoftware/OpenVR, commit `0924064316de3effbcd1acf1e309182a2deb1c05`
  (OpenVR v2.15.6), BSD-3-Clause. [Upstream license at that revision](https://github.com/ValveSoftware/openvr/blob/0924064316de3effbcd1acf1e309182a2deb1c05/LICENSE).
- TsudaKageyu/MinHook, commit `c3fcafdc10146beb5919319d0683e44e3c30d537`
  (MinHook v1.3.4), BSD-2-Clause. MinHook's license also contains the notices
  for Hacker Disassembler Engine 32/64. [Complete upstream license at that revision](https://github.com/TsudaKageyu/minhook/blob/c3fcafdc10146beb5919319d0683e44e3c30d537/LICENSE.txt).

The build copies the complete upstream license texts to `licenses/` in the
driver package.

The graphical Windows installer is built with Inno Setup:

- JRSoftware/Inno Setup 6, Copyright (c) 1997-2026 Jordan Russell and
  Copyright (c) 2000-2026 Martijn Laan, Inno Setup License.

The complete Inno Setup license is included as
`licenses/Inno-Setup-LICENSE.txt`. Inno Setup is a build-time dependency; its
installer runtime is embedded only in the generated Setup and uninstaller.
[Official license](https://jrsoftware.org/files/is/license.txt). Keep Inno Setup's
existing runtime copyright notices and website addresses when redistributing.

The SteamVR interception architecture was informed by OpenVR Space Calibrator:

- `a-popp/openvr-spacecalibrator` and its maintained successor
  `hyblocker/OpenVR-SpaceCalibrator`, MIT License.
- Copyright (c) 2023-2026 Hyblocker and other contributors.
- Copyright (c) 2020-2022 Justin Li and other contributors.

No OpenVR Space Calibrator source file is vendored. Its public MIT-licensed
driver pattern (hooking `IVRDriverContext::GetGenericInterface` and
`IVRServerDriverHost::TrackedDevicePoseUpdated`) is credited here. The complete
MIT notice is included as `licenses/OpenVR-SpaceCalibrator-LICENSE.txt`.
The notice matches the Space Calibrator MIT section of the
[upstream aggregate license](https://github.com/hyblocker/OpenVR-SpaceCalibrator/blob/0cc5e09b5132c4dce2b1ce804a2692df63608766/LICENSE).
Other components listed in that aggregate (such as Eigen and SimpleBLE) are not
included in PoseAnchor merely by referencing its driver architecture.

PoseAnchor's logo and icon SVGs were supplied for this project and are preserved
in `assets/branding/`. The wordmarks contain outlined paths; no font file is
bundled. `scripts/generate-icon.py` converts the primary SVG into the committed
PNG and Windows ICO using sharp (MIT) and Pillow (HPND). These are optional
maintainer-only tools, not distributed runtime dependencies. See
`assets/branding/README.md` for regeneration instructions.

PoseAnchor's own code and documentation are under the MIT license in `LICENSE`.
That license does not replace the terms of the third-party components above.
The packaged `LICENSE`, this notice and all files in `licenses/` should accompany
redistributed binaries. SteamVR itself is not bundled.
