#include "status_parser.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using pose_anchor::status::JsonLookupStatus;
using pose_anchor::status::LocalTimestamp;
using pose_anchor::status::ManifestPathKind;
using pose_anchor::status::ParseOptions;
using pose_anchor::status::RegistrationState;

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

LocalTimestamp timeAt(int second, int millisecond = 0) {
    return {2026, 8, 23, 19, 27, second, millisecond};
}

void timestampTests() {
    const auto parsed = pose_anchor::status::parseSteamVrTimestamp(
        "Sun Aug 23 2026 19:27:59.354 [Info] - message");
    expect(parsed.has_value(), "valid SteamVR timestamp parses");
    expect(parsed && *parsed == timeAt(59, 354), "timestamp fields are exact");
    expect(!pose_anchor::status::parseSteamVrTimestamp(
               "Sun Feb 30 2026 19:27:59.354 [Info] - invalid"),
           "invalid calendar date is rejected");
    expect(!pose_anchor::status::parseSteamVrTimestamp("not a SteamVR line"),
           "non-log line is rejected");
}

void activeSessionTests() {
    const std::string log =
        "Sun Aug 23 2026 19:27:40.000 [Info] - pose_anchor: [PoseAnchor] "
        "IVRServerDriverHost_006 pose hook installed\n"
        "Sun Aug 23 2026 19:27:40.010 [Info] - pose_anchor: [PoseAnchor] "
        "PoseAnchor 0.0.9 loaded (Vive Tracker only, normal poses pass through)\n"
        "Sun Aug 23 2026 19:27:58.500 [Info] - server current session starts\n"
        "Sun Aug 23 2026 19:27:59.350 [Info] - pose_anchor: [PoseAnchor] "
        "IVRServerDriverHost_006 pose hook installed\r\n"
        "Sun Aug 23 2026 19:27:59.354 [Info] - pose_anchor: [PoseAnchor] "
        "PoseAnchor 0.1.0 loaded (Vive Tracker only, normal poses pass through)\n"
        "Sun Aug 23 2026 19:28:00.100 [Info] - pose_anchor: [PoseAnchor] identified Vive "
        "Tracker LHR-B (VIVE Tracker 3.0) driverFromHead q=(1,0,0,0) t=(0,0,0)\n"
        "Sun Aug 23 2026 19:28:00.200 [Info] - pose_anchor: [PoseAnchor] identified Vive "
        "Tracker LHR-A (VIVE Tracker 3.0) driverFromHead q=(1,0,0,0) t=(0,0,0)\n"
        "Sun Aug 23 2026 19:28:00.300 [Info] - pose_anchor: [PoseAnchor] identified Vive "
        "Tracker LHR-A (updated model) driverFromHead q=(1,0,0,0) t=(0,0,0)\n"
        "Sun Aug 23 2026 19:28:01.000 [Info] - pose_anchor: [PoseAnchor] tracker LHR-A "
        "tracking -> hold reasons=0x81 dt_ms=1.0000\n"
        "Sun Aug 23 2026 19:28:02.000 [Info] - pose_anchor: [PoseAnchor] tracker LHR-A "
        "hold -> recovering reasons=0x0 suppressed=2\n"
        "Sun Aug 23 2026 19:28:03.000 [Info] - pose_anchor: [PoseAnchor] tracker LHR-A "
        "recovering -> tracking reasons=0x0\n"
        "Sun Aug 23 2026 19:28:04.000 [Info] - pose_anchor: [PoseAnchor] tracker LHR-B "
        "hold -> lost reasons=0x4\n";

    ParseOptions options;
    options.sessionStart = timeAt(58, 100);
    options.maxRecentEvents = 3;
    const auto result = pose_anchor::status::parseVrServerLog(log, options);
    expect(result.sawCurrentSessionLine, "current session lines are detected");
    expect(result.loaded, "current driver load is active");
    expect(result.hookInstalled, "hook installed before load is associated with load");
    expect(result.version == "0.1.0", "old session version is ignored");
    expect(result.trackers.size() == 2, "tracker serials are unique");
    expect(result.trackers.size() == 2 && result.trackers[0].serial == "LHR-A",
           "trackers are sorted by serial");
    expect(result.trackers.size() == 2 && result.trackers[0].model == "updated model",
           "latest tracker model replaces prior metadata");
    expect(result.holdTransitions == 1, "hold transition counted");
    expect(result.recoveryTransitions == 3, "suppressed recovery transitions counted");
    expect(result.trackingTransitions == 1, "tracking transition counted");
    expect(result.lostTransitions == 1, "lost transition counted");
    expect(result.recentEvents.size() == 3, "recent event list is bounded");
    expect(result.recentEvents.size() == 3 &&
               result.recentEvents.front().toState == "recovering",
           "recent list retains newest events");
}

void staleAndFailureTests() {
    const std::string stale =
        "Sun Aug 23 2026 19:27:40.000 [Info] - pose_anchor: [PoseAnchor] "
        "IVRServerDriverHost_006 pose hook installed\n"
        "Sun Aug 23 2026 19:27:40.010 [Info] - pose_anchor: [PoseAnchor] "
        "PoseAnchor 0.1.0 loaded (Vive Tracker only, normal poses pass through)\n";
    ParseOptions options;
    options.sessionStart = timeAt(58);
    const auto staleResult = pose_anchor::status::parseVrServerLog(stale, options);
    expect(!staleResult.sawCurrentSessionLine, "old log is not current-session evidence");
    expect(!staleResult.loaded && !staleResult.hookInstalled,
           "old load and hook never become active status");

    const std::string disabled =
        "Sun Aug 23 2026 19:27:59.000 [Info] - pose_anchor: [PoseAnchor] "
        "PoseAnchor 0.1.0 loaded (Vive Tracker only, normal poses pass through)\n"
        "Sun Aug 23 2026 19:27:59.001 [Info] - pose_anchor: [PoseAnchor] "
        "filterEnabled is false; no unsupported pose hook was installed\n";
    const auto disabledResult = pose_anchor::status::parseVrServerLog(disabled, options);
    expect(disabledResult.loaded && disabledResult.filterDisabled,
           "disabled filter remains a loaded but inactive driver");
    expect(!disabledResult.hookInstalled, "disabled filter does not claim a hook");

    const std::string failure =
        "Sun Aug 23 2026 19:27:59.000 [Info] - pose_anchor: [PoseAnchor] "
        "PoseAnchor 0.1.0 loaded (Vive Tracker only, normal poses pass through)\n"
        "Sun Aug 23 2026 19:27:59.001 [Info] - pose_anchor: [PoseAnchor] "
        "warning: no SteamVR pose hook was installed; filtering is inactive\n";
    const auto failureResult = pose_anchor::status::parseVrServerLog(failure, options);
    expect(failureResult.loaded && failureResult.hookFailure,
           "hook failure is retained as explicit evidence");
    expect(!failureResult.hookInstalled, "failed hook is not successful");

    const std::string unloaded =
        "Sun Aug 23 2026 19:27:59.000 [Info] - pose_anchor: [PoseAnchor] "
        "IVRServerDriverHost_006 pose hook installed\n"
        "Sun Aug 23 2026 19:27:59.001 [Info] - pose_anchor: [PoseAnchor] "
        "PoseAnchor 0.1.0 loaded (Vive Tracker only, normal poses pass through)\n"
        "Sun Aug 23 2026 19:27:59.500 [Info] - pose_anchor: [PoseAnchor] "
        "warning: no SteamVR pose hook was installed; filtering is inactive\n"
        "Sun Aug 23 2026 19:28:00.000 [Info] - pose_anchor: [PoseAnchor] PoseAnchor unloaded\n";
    const auto unloadedResult = pose_anchor::status::parseVrServerLog(unloaded, options);
    expect(unloadedResult.sawUnloadLine && !unloadedResult.loaded && !unloadedResult.hookInstalled,
           "unload supersedes earlier load and hook");
    expect(!unloadedResult.hookFailure && !unloadedResult.filterDisabled,
           "unload clears inactive-state explanations from the old load");

    const std::string reloaded =
        "Sun Aug 23 2026 19:27:59.000 [Info] - pose_anchor: [PoseAnchor] "
        "IVRServerDriverHost_006 pose hook installed\n"
        "Sun Aug 23 2026 19:27:59.001 [Info] - pose_anchor: [PoseAnchor] "
        "PoseAnchor 0.1.0 loaded (Vive Tracker only, normal poses pass through)\n"
        "Sun Aug 23 2026 19:28:00.000 [Info] - pose_anchor: [PoseAnchor] identified Vive "
        "Tracker LHR-OLD (old model) driverFromHead q=(1,0,0,0) t=(0,0,0)\n"
        "Sun Aug 23 2026 19:28:01.000 [Info] - pose_anchor: [PoseAnchor] tracker LHR-OLD "
        "tracking -> hold reasons=0x1\n"
        "Sun Aug 23 2026 19:28:02.000 [Info] - pose_anchor: [PoseAnchor] PoseAnchor unloaded\n"
        "Sun Aug 23 2026 19:28:03.000 [Info] - pose_anchor: [PoseAnchor] tracker LHR-OLD "
        "hold -> lost reasons=0x4\n"
        "Sun Aug 23 2026 19:28:04.000 [Info] - pose_anchor: [PoseAnchor] "
        "IVRServerDriverHost_006 pose hook installed\n"
        "Sun Aug 23 2026 19:28:04.001 [Info] - pose_anchor: [PoseAnchor] "
        "PoseAnchor 0.1.1 loaded (Vive Tracker only, normal poses pass through)\n"
        "Sun Aug 23 2026 19:28:05.000 [Info] - pose_anchor: [PoseAnchor] identified Vive "
        "Tracker LHR-NEW (new model) driverFromHead q=(1,0,0,0) t=(0,0,0)\n"
        "Sun Aug 23 2026 19:28:06.000 [Info] - pose_anchor: [PoseAnchor] tracker LHR-NEW "
        "hold -> recovering reasons=0x0\n";
    const auto reloadedResult = pose_anchor::status::parseVrServerLog(reloaded, options);
    expect(reloadedResult.loaded && reloadedResult.hookInstalled &&
               reloadedResult.version == "0.1.1",
           "new load epoch becomes current after unload and reload");
    expect(reloadedResult.trackers.size() == 1 &&
               reloadedResult.trackers[0].serial == "LHR-NEW",
           "reload drops tracker metadata from the unloaded copy");
    expect(reloadedResult.holdTransitions == 0 && reloadedResult.lostTransitions == 0 &&
               reloadedResult.recoveryTransitions == 1 &&
               reloadedResult.recentEvents.size() == 1,
           "reload drops old and post-unload intervention events");
}

void resourceBoundTests() {
    std::string log =
        "Sun Aug 23 2026 19:27:59.001 [Info] - pose_anchor: [PoseAnchor] "
        "PoseAnchor 0.1.0 loaded (Vive Tracker only, normal poses pass through)\n";
    for (int index = 0; index < 70; ++index) {
        log += "Sun Aug 23 2026 19:28:00.100 [Info] - pose_anchor: [PoseAnchor] "
               "identified Vive Tracker LHR-" + std::to_string(index) +
               " (VIVE Tracker 3.0) driverFromHead q=(1,0,0,0) t=(0,0,0)\n";
    }
    log += "Sun Aug 23 2026 19:28:00.200 [Info] - pose_anchor: [PoseAnchor] "
           "identified Vive Tracker " + std::string(300, 'X') +
           " (oversized) driverFromHead q=(1,0,0,0) t=(0,0,0)\n";

    const auto result = pose_anchor::status::parseVrServerLog(log);
    expect(result.trackers.size() == 64 && result.trackersTruncated,
           "tracker display input is capped at OpenVR's 64-device limit");
}

void jsonTests() {
    const std::string paths = R"({
        "config": ["C:\\Steam\\config"],
        "external_drivers": ["C:\\Users\\Me\\PoseAnchor\\", "D:/VR/Driver"],
        "runtime": ["C:\\SteamVR"],
        "unicode": "Pose\u0041nchor"
    })";
    const auto drivers = pose_anchor::status::findJsonStringArray(paths, "external_drivers");
    expect(drivers.status == JsonLookupStatus::Found && drivers.values.size() == 2,
           "OpenVR external driver array parses");
    expect(drivers.values.size() == 2 && drivers.values[0] == "C:\\Users\\Me\\PoseAnchor\\",
           "JSON path escapes decode");
    const auto unicode = pose_anchor::status::findJsonString(paths, "unicode");
    expect(unicode.status == JsonLookupStatus::Found && unicode.value == "PoseAnchor",
           "JSON unicode escape decodes");
    expect(pose_anchor::status::findJsonString(paths, "missing").status ==
               JsonLookupStatus::Missing,
           "missing JSON member is distinct from malformed JSON");
    const auto noExternalDrivers = pose_anchor::status::findJsonStringArray(
        R"({"runtime":["C:\\SteamVR"]})", "external_drivers");
    expect(noExternalDrivers.status == JsonLookupStatus::Missing,
           "valid registration JSON without external_drivers is a missing member");
    const auto emptyExternalDrivers = pose_anchor::status::findJsonStringArray(
        R"({"external_drivers":[]})", "external_drivers");
    expect(emptyExternalDrivers.status == JsonLookupStatus::Found &&
               emptyExternalDrivers.values.empty(),
           "empty external driver list is valid JSON");
    expect(pose_anchor::status::findJsonStringArray(
               R"({"external_drivers":["C:\\ok",]})", "external_drivers").status ==
               JsonLookupStatus::Malformed,
           "malformed driver registration file is rejected");
    expect(pose_anchor::status::findJsonString(
               R"({"name":"pose_anchor","name":"duplicate"})", "name").status ==
               JsonLookupStatus::Malformed,
           "duplicate security-relevant member is rejected");

    std::string tooDeep = "{\"other\":";
    tooDeep.append(65, '[');
    tooDeep += '0';
    tooDeep.append(65, ']');
    tooDeep += '}';
    expect(pose_anchor::status::findJsonString(tooDeep, "name").status ==
               JsonLookupStatus::Malformed,
           "excessively nested JSON is rejected without unbounded recursion");
}

void pathTests() {
    expect(pose_anchor::status::normalizeWindowsPath(
               "  \"C:/Users/ME/PoseAnchor/\"  ") == "c:\\users\\me\\poseanchor",
           "path comparison ignores case, slash style, quotes, and trailing slash");
    expect(pose_anchor::status::normalizeWindowsPath("\\\\Server\\Share\\") ==
               "\\\\server\\share",
           "UNC prefix is preserved");
    expect(pose_anchor::status::normalizeWindowsPath("C:\\\\VR\\\\Driver") ==
               "c:\\vr\\driver",
           "duplicate non-UNC separators collapse");

    expect(pose_anchor::status::classifyWindowsManifestPath(
               "\\\\Server\\Share\\PoseAnchor") == ManifestPathKind::Remote,
           "UNC driver root is classified as remote without filesystem access");
    expect(pose_anchor::status::classifyWindowsManifestPath(
               "//Server/Share/PoseAnchor") == ManifestPathKind::Remote,
           "forward-slash UNC driver root is classified as remote");
    expect(pose_anchor::status::classifyWindowsManifestPath(
               "\\\\?\\UNC\\Server\\Share\\PoseAnchor") == ManifestPathKind::Remote,
           "extended UNC driver root is classified as remote");
    expect(pose_anchor::status::classifyWindowsManifestPath(
               "Z:\\VR\\PoseAnchor", true) == ManifestPathKind::Remote,
           "platform-reported mapped drive is classified as remote");
    expect(pose_anchor::status::classifyWindowsManifestPath(
               "C:\\VR\\PoseAnchor") == ManifestPathKind::Local,
           "ordinary local driver root remains probeable");
    expect(pose_anchor::status::classifyWindowsManifestPath(
               "\\\\?\\C:\\VR\\PoseAnchor") == ManifestPathKind::Local,
           "extended local drive root remains probeable");
    expect(pose_anchor::status::classifyWindowsManifestPath(
               "\\\\.\\pipe\\PoseAnchor") == ManifestPathKind::Unsupported,
           "device namespace is not opened as a driver root");

    expect(pose_anchor::status::registrationStateFromInspection(1, 0, false) ==
               RegistrationState::Registered,
           "one expected local registration is registered");
    expect(pose_anchor::status::registrationStateFromInspection(0, 1, false) ==
               RegistrationState::RegisteredElsewhere,
           "one other local PoseAnchor root remains registered elsewhere");
    expect(pose_anchor::status::registrationStateFromInspection(0, 2, false) ==
               RegistrationState::ConflictingRegistrations,
           "multiple other PoseAnchor roots remain conflicting");
    expect(pose_anchor::status::registrationStateFromInspection(1, 1, false) ==
               RegistrationState::ConflictingRegistrations,
           "local expected and other copies remain conflicting");
    expect(pose_anchor::status::registrationStateFromInspection(2, 0, false) ==
               RegistrationState::ConflictingRegistrations,
           "duplicate expected local registrations remain conflicting");
    expect(pose_anchor::status::registrationStateFromInspection(1, 0, true) ==
               RegistrationState::InspectionIncomplete,
           "skipped remote manifest prevents a positive registration result");
    expect(pose_anchor::status::registrationStateFromInspection(1, 1, true) ==
               RegistrationState::InspectionIncomplete,
           "incomplete inspection takes precedence over known local conflicts");
    expect(pose_anchor::status::registrationStateFromInspection(0, 0, true) ==
               RegistrationState::InspectionIncomplete,
           "remote-only registration evidence fails closed");
}

}  // namespace

int main() {
    timestampTests();
    activeSessionTests();
    staleAndFailureTests();
    resourceBoundTests();
    jsonTests();
    pathTests();

    if (failures != 0) {
        std::cerr << failures << " status parser test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All status parser tests passed\n";
    return EXIT_SUCCESS;
}
