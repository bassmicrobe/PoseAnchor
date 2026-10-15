# PoseAnchor installer

`PoseAnchor.iss` builds the command-free Windows distribution with Inno Setup 6.
It installs per user under `%LOCALAPPDATA%\Programs\PoseAnchor`, registers that
folder with SteamVR's own `vrpathreg.exe`, and creates Inno Setup's uninstaller.

The registration flow follows Valve's driver guidance:

1. Locate SteamVR from its uninstall registry key, with Steam registry/default
   path fallbacks.
2. Run `finddriver pose_anchor` before changing registration.
3. Keep an existing registration if it already points at `{app}`.
4. Before changing files, ask before replacing one older PoseAnchor path or
   using `removedriverswithname pose_anchor` for multiple old paths.
5. Before adding or replacing registration, save and SHA-256-verify a byte-exact backup
   of `openvrpaths.vrpath`; abort if it changes while the wizard is open.
6. If adding or verifying `{app}` fails, restore and hash-check that exact
   backup. Captured paths are used only as a best-effort fallback.
7. Verify the installed path with a final `finddriver`.
8. Run `removedriver {app}` before uninstalling files and verify that
   `finddriver pose_anchor` no longer reports the installed path.

Setup never closes SteamVR or PoseAnchor Status automatically. A short
language-specific safety page is shown before installation, and a running
`vrserver.exe` or `PoseAnchorStatus.exe` blocks setup and uninstall before file
or registration changes. Both processes are checked again at the registration
mutation point.
Process inspection errors are reported separately from a detected running app.
Both block setup/uninstall, but an inspection failure gives restart/support
guidance instead of asking the user to close a process that may not exist.

Setup and uninstall reject `VR_PATHREG_OVERRIDE`: their backup and rollback use
the standard per-user OpenVR registration file. Remove that override and launch
them from a new session. They do not modify a different registry behind the
backup's protection.

Build the installer from the repository root with
`scripts\build-installer.ps1`. The generated EXE and SHA-256 checksum are placed
in `dist\`. Inno Setup is required only on the build machine.
The staged `build-info.json` must match the requested configuration and current
project version, including when using `-SkipBuild`. Rebuild the package if the
metadata is missing or mismatched.

The locally generated installer is unsigned by default. It can be installed
without Authenticode signing where Windows policy permits; Windows may show an
unknown-publisher warning, and managed or protected environments may block it.
Signing is optional for distribution and does not guarantee the absence of
reputation warnings. To sign, pass `-CertificateThumbprint <sha1>` for a
code-signing certificate in the current user's store to `build-installer.ps1`.
The script signs
the installed driver DLL and on-demand status executable first, then supplies a
named signing tool to Inno Setup so both Setup and its embedded uninstaller are
signed before the final checksum is written. A requested build fails if any
installed executable or the completed Setup signature is invalid. Do not publish
private keys or certificate passwords in this repository.
