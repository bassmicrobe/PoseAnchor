// Exercise the actual native presentation without probing SteamVR or changing
// registration. The optional preview is a test-only binary, never packaged.
#define wWinMain poseAnchorProductionEntryForTests
#include "../src/status/status_main.cpp"
#undef wWinMain

#include <iostream>

using namespace pose_anchor::status;

int wmain(int argc, wchar_t** argv) {
    RegistrationInfo registration;
    registration.state = RegistrationState::Registered;
    registration.expectedRoot = L"C:\\PoseAnchor test fixture";
    registration.registrationFile = L"C:\\PoseAnchor test fixture\\openvrpaths.vrpath";
    RuntimeStatus runtime;
    runtime.process.running = true;
    runtime.process.inspectionComplete = true;
    runtime.process.processId = 123;
    runtime.process.start = LocalTimestamp{2026, 10, 15, 12, 0, 0, 0};
    runtime.logPath = L"C:\\PoseAnchor test fixture\\logs\\vrserver.txt";
    runtime.log.exists = runtime.log.opened = true;
    runtime.loaded = runtime.hook = EvidenceState::Yes;
    runtime.completeSessionCoverage = true;
    runtime.session.loaded = runtime.session.hookInstalled = true;
    runtime.session.sawCurrentSessionLine = true;
    runtime.session.version = "0.1.0";
    runtime.session.holdTransitions = 12345;
    runtime.session.recoveryTransitions = 12344;
    runtime.session.lostTransitions = 100;
    for (int index = 0; index < 64; ++index) {
        runtime.session.trackers.push_back({"LHR-123456789ABCDEF-" + std::to_string(index),
                                           "VIVE Tracker 3.0 test fixture"});
    }
    for (int index = 0; index < 5; ++index) {
        runtime.session.recentEvents.push_back({{2026, 10, 15, 12, 1, index, 123},
            "LHR-123456789ABCDEF-0", "tracking", "recovering", 0, 123});
    }

    if (argc == 2 && (std::wstring_view(argv[1]) == L"--preview-ja" ||
                      std::wstring_view(argv[1]) == L"--preview-en")) {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        DialogContext context;
        context.japanese = std::wstring_view(argv[1]) == L"--preview-ja";
        context.expanded = true;
        context.text = buildUiText(registration, runtime, context.japanese);
        context.text.title += L" [TEST FIXTURE]";
        configureDialog(context);
        context.config.pfCallback = [](HWND window, UINT notification, WPARAM, LPARAM, LONG_PTR) -> HRESULT {
            if (notification == TDN_CREATED) {
                for (const int button : {kRefreshButton, kOpenLogButton, kOpenReadmeButton}) {
                    SendMessageW(window, TDM_ENABLE_BUTTON, button, FALSE);
                }
            }
            return S_OK;
        };
        return showStatusDialog(context);
    }

    int failures = 0;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; ++failures; }
    };
    for (const bool japanese : {false, true}) {
        const auto text = buildUiText(registration, runtime, japanese);
        check(!text.warning, "Verified ready state should not warn");
        check(text.content.find(japanese ? L"現在の接続数" : L"not a live connection count") != std::wstring::npos,
              "Recognition history must not imply current connections");
        check(text.content.find(L"LHR-") == std::wstring::npos,
              "Tracker identities belong in details, not the primary summary");
        check(text.details.find(L"LHR-123456789ABCDEF-63") == std::wstring::npos,
              "Tracker detail rendering must be bounded");
        check(text.footer.find(japanese ? L"確認時刻" : L"Checked:") != std::wstring::npos,
              "Snapshot must include checked-at time");
        check(text.details.find(japanese ? L"（下限）" : L"(minimum)") != std::wstring::npos,
              "Even complete logs cannot prove exact transition totals");
        DialogContext context;
        context.japanese = japanese;
        context.text = text;
        configureDialog(context);
        check((context.config.dwFlags & TDF_CALLBACK_TIMER) == 0, "Do not add idle timer polling");
        context.expanded = true;
        configureDialog(context, true);
        check((context.config.dwFlags & TDF_EXPANDED_BY_DEFAULT) != 0,
              "Refresh must preserve expanded diagnostics");
        check(context.config.nDefaultButton == kRefreshButton, "Refresh keeps keyboard default on refresh");
    }
    runtime.session.trackers.clear();
    check(buildUiText(registration, runtime, false).mainInstruction == L"Waiting for a tracker",
          "Loaded without recognition must not claim tracker readiness");
    runtime.process.inspectionComplete = false;
    for (const bool running : {false, true}) {
        runtime.process.running = running;
        for (const bool japanese : {false, true}) {
            const auto unknown = buildUiText(registration, runtime, japanese);
            check(unknown.mainInstruction == (japanese ? L"SteamVR の状態を確認できません"
                                                       : L"SteamVR state could not be verified"),
                  "Failed process scan must get an actionable unknown-state headline");
            check(unknown.content.find(japanese ? L"SteamVR: 停止中" : L"SteamVR: Stopped") == std::wstring::npos,
                  "Failed scan must not tell users SteamVR is stopped");
        }
    }
    runtime.process.inspectionComplete = runtime.process.running = true;
    runtime.session.filterDisabled = true;
    check(buildUiText(registration, runtime, false).mainInstruction == L"Filtering is disabled in settings",
          "Explicitly disabled filtering must not claim readiness");
    registration.state = RegistrationState::BrokenInstallation;
    check(buildUiText(registration, runtime, false).content.find(L"Close this window and SteamVR") != std::wstring::npos,
          "Repair instructions must close both locking processes");
    if (!failures) std::cout << "Status UI tests passed\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
