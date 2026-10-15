# Performance and UI/UX audit — 2026-10-15

Follow-up audit of the complete current working tree: filter, watchdog, driver hooks/registry/adapter, status parser and native UI, installer/uninstaller guards, documentation and benchmarks. Existing uncommitted work from the earlier audits was preserved. No installed driver, SteamVR registration or Windows settings were changed; no commit, push or publication was performed.

## Findings and fixes

1. **Repeated work after tracking loss.** The watchdog recomputed the same bounded prediction on each Lost tick. Cache the exact existing result when entering Lost instead. The hold anchor cannot change during Lost; reacquisition, a subsequent loss and reset still update/clear it. No new storage, allocation or change to filter decisions.
2. **Calibration diagnostic flooding.** Every changed world/device-origin transform could allocate a log string and synchronously call SteamVR logging while pose locks were held. Limit each of these two diagnostic categories independently to one per second per device. The first message and every safety reset/transform update remain intact. Unchanged-transform callbacks do not invoke the limiter. Fixed storage cost: 2 KiB for all 64 device slots; no timer/thread.
3. **Unknown process state shown as stopped.** Native process enumeration/open/timing failures could become a stopped/ready status. Track inspection completeness and show an actionable unknown state instead. An unreadable matching process also prevents choosing another, potentially older instance's log as proof of readiness.
4. **Installer inspection failure shown as running.** Preserve fail-closed setup/uninstall behavior, but distinguish a process actually running from failed inspection. Handle process-launch errors, exit failures, capture exceptions and incomplete output. Users are no longer told only to close an app when the check itself failed.
5. **Counts and effect claims.** Log suppression can leave unflushed transitions even when a full session log is available. Event counts are now explicitly lower bounds in Japanese/English, not complete totals or evidence of tracking improvement. Clarify Hold as brief prediction and remove guaranteed-effect wording from setup instructions.

No additional confirmed defect was found in the normal adapter/registry path or parser within this review. Their bounded/log-on-demand designs are retained. No new Rust/AI/runtime dependency was needed.

## Performance evidence

Windows x64, MSVC 19.44 Release; seven alternating before/after runs for the changed filter path. Other desktop applications were running, so timings are measurements on this machine, not guarantees.

| Microbenchmark | Before median | After median |
|---|---:|---:|
| Lost watchdog tick | 57.17 ns | 13.79 ns |
| Normal Tracking push | 108.37 ns | 108.84 ns |
| Disturbed push | 147.07 ns | 144.86 ns |

The Lost tick is about 76% cheaper; ordinary tracking is effectively unchanged. Each Lost run produced 1,000,000 invalid outputs, exactly one loss entry and zero allocations. Filter size remains 1,696 bytes. These are **individual processing stages, not whole-SteamVR CPU savings or physical tracking improvements**.

Independent integrated seven-run checks: Tracking 104.62 ns/sample, disturbed 141.79 ns/sample, Lost 14.03 ns/tick, 64-slot scan 62.23 ns/frame, adapter pipeline 85.13 ns/sample. All timed filter phases allocated zero times and passed scenario assertions.

The diagnostic limiter regression models 200 Hz for two minutes: 24,000 decisions permit 120 messages per category, rather than logging each change. This is a deterministic limiter test, not a measured disk-throughput or CPU reduction.

The unchanged parser was remeasured with approximately 16 MiB synthetic logs: ordinary 6.731 ms / 3 allocations; event-heavy history 5: 15.860 ms / 13 allocations; history 256: 14.484 ms / 274 allocations (eleven-run medians). This checks that the earlier bounded-history optimization remains intact; it is not a new before/after comparison.

## UI/UX and verification

- New test-only native presentation harness checks the actual Japanese and English text, ready/waiting/disabled/unknown states, historical-count caveat, lower-bound counts, repair instructions, bounded identity rendering, timestamp, no timer flag and preserved expanded state.
- Optional `pose_anchor_status_ui_tests.exe --preview-ja` / `--preview-en` renders deterministic synthetic data, without registering a driver or faking a live SteamVR process. It is not included in the package. Default CTest invocation does not show UI or inspect live SteamVR.
- Inspected native Japanese and English expanded layouts with 64 synthetic identities (six displayed) and five recent events. Status/action hierarchy, counts, paths and Close control remained visible on the current display. Common Windows buttons follow the OS language.
- Final packaged status app correctly showed the separately installed copy and stopped SteamVR; manual Refresh updated the checked-at timestamp. Closed only the audit-launched build; the user's separately opened installed Status app was left untouched.
- Release CTest: **8/8 passed**. Driver-independent ASan/UBSan CTest: **4/4 passed**; the PowerShell member is not sanitizer-instrumented.
- Installer-script checks passed in Windows PowerShell 5.1 and PowerShell 7. An initial cross-version test failure exposed implicit text encoding; the test now explicitly reads the Inno source as UTF-8.
- Reviewed Win32 enumeration handling against [Process32NextW documentation](https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-process32nextw), and capture-failure handling against [Inno Setup's ExecAndCaptureOutput documentation](https://jrsoftware.org/ishelp/topic_isxfunc_execandcaptureoutput.htm).

The final installer-script suite has 76 passing checks under each PowerShell version. Inno Setup 6.7.3 compiled the final installer; final Release CTest again passed 8/8. Staged Status/README hashes match their current build/source inputs, and `git diff --check` passed.

After detaching the UI inspection session and a five-second startup settling interval, the audit-launched Status app accumulated 0.03125 CPU seconds over 20 seconds (about 0.16% of one logical core), using 3,956,736 private bytes and 24,821,760 working-set bytes at the end. This short SteamVR-stopped desktop sample is not a long-session or VR-wide benchmark. OS/automation effects and measurement granularity still apply.

## Local release candidate

`dist/perf-ux-audit-2026-10-15/PoseAnchor-Setup-0.1.0.exe`

SHA-256: `21a0b665f33f87e69eaef358aa693a25f5de1ddbcb74741a9957ef947c5f5d7c`

Authenticode: **NotSigned**. The installer includes its uninstaller, as before. This file was built locally, not installed or published.

## Validation boundaries

No live VR session, occlusion test, recovery ON/OFF comparison, continuous physical calibration or long-session soak test was performed. Installer failure/rollback paths were source/contract/compile-checked, not triggered against the user's real installation. All DPI sizes and assistive technologies have not been exhaustively exercised. The UI remains manually refreshed and does not add automatic polling.
