# PoseAnchor installer

`PoseAnchor.iss` builds the command-free Windows distribution with Inno Setup 6.
It installs per user under `%LOCALAPPDATA%\Programs\PoseAnchor`, registers that
folder with SteamVR's own `vrpathreg.exe`, and creates Inno Setup's uninstaller.
The uninstaller is embedded in the Setup EXE and installed automatically. Users
can remove PoseAnchor through Windows Settings → Apps → Installed apps, or the
**Uninstall PoseAnchor** Start-menu entry; no command or separate download is needed.

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
8. Before uninstalling files, inspect this exact path. If already absent, no
   registration write is needed. Otherwise run `removedriver {app}` and verify
   that `finddriver pose_anchor` no longer reports the installed path.

Uninstallation only removes the registration for its own exact installation
path. It does not use name-wide removal, change another PoseAnchor copy or other
drivers, or delete SteamVR user settings and logs. If unregistration or its
verification fails, it aborts before file deletion, including unattended removal;
there is no continue-anyway override. Follow the error and retry after resolving
it rather than deleting the registered folder manually.

Inno Setup removes the files and shortcuts recorded by the installation; there
is no recursive installation-directory wipe. Separately added files may remain
and keep the directory from being removed. Edits to originally installed files
do not exempt those files from removal, so back them up elsewhere if needed.
The end-user steps are in the bundled [manual](../MANUAL.md).

On a Windows build machine with Inno Setup, run
`tests\uninstaller_runtime_tests.ps1` for the isolated runtime regression.
It copies the production uninstall events and path helpers into an inert fixture,
with mocked registration/process inspection and no Windows Apps registry entry.
The fixture checks refusal paths and verifies that successful removal deletes
only its installed payload while preserving a separately added file. Test files
are retained under a fresh ignored `build/uninstall-safety-*` directory.
`-CompileOnly` compiles without executing the fixture. Windows CI runs the full
fixture after building the installer. This does not replace real SteamVR
registration, upgrade, permission, or unusual-path testing on a disposable host.

Setup never closes SteamVR or PoseAnchor Status automatically. A short
language-specific safety page is shown before installation, and a running
`vrserver.exe` or `PoseAnchorStatus.exe` blocks setup and uninstall before file
or registration changes. Both processes are checked again at the registration
mutation point.
Process inspection errors are reported separately from a detected running app.
Both block setup/uninstall, but an inspection failure gives restart/support
guidance instead of asking the user to close a process that may not exist.

Setup and uninstall reject `VR_PATHREG_OVERRIDE` and only support the standard
per-user OpenVR registration file. Setup's backup and rollback target that file;
uninstall does not restore the entire registry, which could overwrite unrelated
concurrent changes. Remove the override and launch from a new session before
retrying. A failed uninstall retains the installed files, but this copy's
registration may already have been removed.

Build the installer from the repository root with
`scripts\build-installer.ps1`. The generated EXE and SHA-256 checksum are placed
in `dist\`. Inno Setup is required only on the build machine.
The staged `build-info.json` must match the requested configuration and current
project version, including when using `-SkipBuild`. Rebuild the package if the
metadata is missing or mismatched.
Packaging also rejects stale README/manual/license inputs, including with
`-SkipBuild`. Restage the package after changing distribution documents.
The same check covers the application ICO and `assets/branding/`. Supplied logo
SVGs are bundled so the README works offline. Setup, the Status executable,
Windows Apps and the uninstall shortcut share the primary dark-background icon.
Normal Windows builds do not run Python, Node.js, or an SVG renderer.

The current build is experimental. Setup and the Windows Apps entry say so;
both language-specific pre-install pages explain that controlled ON/OFF benefits
are unproven and false detections or predictions can worsen motion. Keep this
wording in release descriptions, mark GitHub releases as Pre-release, and retain
the untested-live-hardware caveats in the packaged README and manual. See the
[experimental release preparation record](../docs/experimental-release-2026-10-15.md).
Keep only the current Setup and matching SHA-256 file in `dist/`; archive older
candidates and diagnostic logs privately outside the repository before release.

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
