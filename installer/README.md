# PoseAnchor installer

`PoseAnchor.iss` builds the command-free Windows distribution with Inno Setup 6.
It installs per user under `%LOCALAPPDATA%\Programs\PoseAnchor`, registers that
folder with SteamVR's own `vrpathreg.exe`, and creates Inno Setup's uninstaller.

The registration flow follows Valve's driver guidance:

1. Locate SteamVR from its uninstall registry key, with Steam registry/default
   path fallbacks.
2. Run `finddriver pose_anchor` before changing registration.
3. Keep an existing registration if it already points at `{app}`.
4. Remove one older PoseAnchor path explicitly before adding `{app}`.
5. If multiple stale PoseAnchor registrations exist, ask before using
   `removedriverswithname pose_anchor`.
6. Verify the installed path with a final `finddriver`.
7. Run `removedriver {app}` before uninstalling files.

Build the installer from the repository root with
`scripts\build-installer.ps1`. The generated EXE and SHA-256 checksum are placed
in `dist\`. Inno Setup is required only on the build machine.

The locally generated development installer is unsigned. Before a public
release, sign both Setup and its embedded uninstaller with an Authenticode
certificate using Inno Setup's `SignTool`/`SignedUninstaller` support. Do not
publish private keys or certificate passwords in this repository.
