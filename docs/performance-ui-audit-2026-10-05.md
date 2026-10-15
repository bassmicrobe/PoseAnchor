# Performance and UI/UX audit — 2026-10-05

Scope: targeted follow-up to the release audit, preserving the existing uncommitted fixes. No installation, SteamVR registration change, commit, or push was performed. The installed driver was not replaced.

## Changes

- **Static coordinate transforms:** skip normalization, square roots and angular-distance calculations only when every component is exactly unchanged. Changed transforms retain the previous 1 mm / 0.1 degree tolerances. Filtering and synchronization semantics are unchanged.
- **Log parser:** parse intermediate identities/events as views and reuse a circular history buffer. Returned data remains owned and chronological. Reject malformed timestamp fractions and cap the version token at 64 characters.
- **Status UX:** add explicit Refresh / 再確認 and local checked-at time. Separate tracker-waiting, settings-disabled, registration problems, driver-load problems and ready states. Show a relevant next action; do not claim live tracking quality or current connectivity from historical logs. Put identities, events and paths in expandable details. No timer, background worker, or automatic polling was added.
- **Recovery guidance:** display the actual setup log filename on installation errors, offer an unchecked failure-only log-opening action, and provide matching Japanese/English README instructions. Repair/uninstall instructions say to close the Status window too.
- **Benchmark reporting:** allow a signed difference between two independently timed pose-forwarding cases while still requiring finite nonnegative direct timings. Validate the new adapter metrics and zero unexpected transform changes.

## Before/after microbenchmarks

MSVC 19.44, Windows x64 Release. These results describe specific processing stages and synthetic logs, **not SteamVR-wide CPU usage, physical tracking accuracy or improvement in VR**. Other applications were running; results are not universal latency guarantees.

Adapter: seven alternating before/after pairs, ten million samples per run; medians:

| Processing stage | Before | After |
|---|---:|---:|
| Read/check two static transforms | 57.92 ns | 25.17 ns |
| Above plus conversion to a filter sample | 111.89 ns | 81.49 ns |

The second stage improved by about 27%. Unexpected transform-change detections were zero. Normal/changed/antipodal and tolerance-boundary tests cover both transform types.

Parser: approximately 16 MiB per log, eleven-run medians. Long serials exceed the standard-library small-string optimization in event-heavy cases.

| Log / retained events | Before | After | Allocations before → after |
|---|---:|---:|---:|
| Ordinary / 5 | 6.804 ms | 5.698 ms | 5 → 3 |
| Event-heavy / 5 | 24.887 ms | 13.325 ms | 248,554 → 13 |
| Event-heavy / 256 | 165.002 ms | 13.422 ms | 248,564 → 274 |

The normal UI retains five recent events. The 256-event case exercises parser scaling rather than the default UI workload. A second run from the integrated build measured 5.593 / 13.646 / 13.896 ms respectively, with the same allocation counts.

The integrated seven-run benchmark passed all correctness checks and reported zero allocations in the timed PoseFilter scenarios. Tracking push median was 101.38 ns/sample; disturbed push was 141.15 ns/sample. These are not end-to-end driver costs.

## Verification

- MSVC Release build and CTest: **7/7 passed**, including new state-summary regression tests.
- Local driver-independent ASan/UBSan build and CTest: **4/4 passed** (core, parser, overview, installer-script tests; the PowerShell test itself is not sanitizer-instrumented).
- Parser regression cases cover history limits 0/1/3/5/20/25, ring wrap order and fields, reload reset, malformed timestamps and oversized versions.
- Native Japanese UI: inspected the installed-copy mismatch/SteamVR-stopped state, manual refresh timestamp changes, details expansion, refresh preserving expanded details, and closing the process. No physical trackers were started for the audit.
- Windows task-dialog navigation rebuilds its controls; button availability is restored on navigation, prior text lives through the synchronous operation, and expanded state is retained. See [Microsoft's navigation documentation](https://learn.microsoft.com/en-us/windows/win32/controls/tdm-navigate-page).
- Setup log paths use Inno Setup's actual [`{log}` constant](https://jrsoftware.org/ishelp/topic_consts.htm), including an explicitly supplied log destination.
- Final Release rebuild: **7/7 tests passed** again. Inno Setup 6.7.3 compiled the installer successfully; staged Status executable and README SHA-256 values match their current build/source inputs. `git diff --check` passed.

## Local release candidate

`dist/perf-ux-audit-2026-10-05/PoseAnchor-Setup-0.1.0.exe`

SHA-256: `98bcbe901dd9630cc6728d0c3d2e3af865aa836f16490e9a1ccdf48e0d2d4383`

Authenticode: **NotSigned**. Built locally only; not installed, published or pushed. The installer includes the uninstaller, as before.

## Remaining validation boundaries

Live Vive Tracker recovery, base-station occlusion, SteamVR-wide CPU, and long VR-session behavior require hardware testing. This audit does not prove an ON/OFF tracking-quality improvement. English and additional DPI/screen-reader combinations were source-reviewed, not exhaustively exercised on live desktops. Installer failure/rollback UI was compile-reviewed rather than triggered against the user's actual SteamVR installation.

UI-automation-assisted process CPU samples were noisy: automation DLLs were loaded and CPU activity was observed during inspection. After resetting the automation session and allowing the desktop to settle, a simultaneous 20-second sample of the installed August build and the new build recorded **0 additional CPU seconds in both processes** at Windows' measurement resolution. The new build used 5,185,536 private bytes and 25,587,712 working-set bytes; these include OS/UI-automation effects and are not a clean memory-regression comparison. This short sample is not a zero-CPU guarantee or a SteamVR-running benchmark. Both audit-launched status instances were closed afterward.
