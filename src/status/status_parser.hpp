#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pose_anchor::status {

// SteamVR writes local wall-clock timestamps at the beginning of vrserver.txt
// lines. Keeping this as a plain value makes session filtering deterministic and
// lets the parser remain independent from Windows APIs.
struct LocalTimestamp {
    int year{};
    int month{};
    int day{};
    int hour{};
    int minute{};
    int second{};
    int millisecond{};

    [[nodiscard]] bool valid() const noexcept;
    auto operator<=>(const LocalTimestamp&) const = default;
};

struct TrackerInfo {
    std::string serial;
    std::string model;
};

struct TransitionEvent {
    LocalTimestamp timestamp{};
    std::string serial;
    std::string fromState;
    std::string toState;
    std::uint32_t reasons{};
    std::uint32_t suppressed{};
};

struct SessionSummary {
    bool sawCurrentSessionLine{};
    bool sawLoadLine{};
    bool sawUnloadLine{};
    bool loaded{};
    bool hookInstalled{};
    bool hookFailure{};
    bool filterDisabled{};
    std::string version;

    std::size_t holdTransitions{};
    std::size_t recoveryTransitions{};
    std::size_t lostTransitions{};
    std::size_t trackingTransitions{};
    std::vector<TrackerInfo> trackers;
    bool trackersTruncated{};
    std::vector<TransitionEvent> recentEvents;

    std::optional<LocalTimestamp> oldestTimestamp;
    std::optional<LocalTimestamp> newestTimestamp;
};

struct ParseOptions {
    // Lines before this timestamp belong to an earlier vrserver process and are
    // ignored. With no boundary, the entire supplied log is parsed.
    std::optional<LocalTimestamp> sessionStart;
    std::size_t maxRecentEvents{5};
};

[[nodiscard]] std::optional<LocalTimestamp> parseSteamVrTimestamp(
    std::string_view line) noexcept;

[[nodiscard]] SessionSummary parseVrServerLog(
    std::string_view utf8Log, const ParseOptions& options = {});

enum class JsonLookupStatus {
    Found,
    Missing,
    Malformed,
};

struct JsonStringResult {
    JsonLookupStatus status{JsonLookupStatus::Missing};
    std::string value;
};

struct JsonStringArrayResult {
    JsonLookupStatus status{JsonLookupStatus::Missing};
    std::vector<std::string> values;
};

enum class ManifestPathKind {
    Local,
    Remote,
    Unsupported,
};

enum class RegistrationState {
    Registered,
    NotRegistered,
    RegisteredElsewhere,
    ConflictingRegistrations,
    InspectionIncomplete,
    BrokenInstallation,
    Unknown,
};

// These deliberately small JSON helpers cover OpenVR's openvrpaths.vrpath and
// driver.vrdrivermanifest files without adding a runtime dependency. They still
// validate the containing JSON structure and never report Found for malformed
// input.
[[nodiscard]] JsonStringResult findJsonString(
    std::string_view json, std::string_view key);
[[nodiscard]] JsonStringArrayResult findJsonStringArray(
    std::string_view json, std::string_view key);

// Stable comparison form for Windows paths. This is lexical only: it performs
// no filesystem access, so it is safe and easy to unit-test on every platform.
[[nodiscard]] std::string normalizeWindowsPath(std::string_view path);

// Performs lexical path screening only. The caller supplies the result of its
// platform-specific mapped-drive check, keeping UNC and registration-state
// policy deterministic and testable on non-Windows CI.
[[nodiscard]] ManifestPathKind classifyWindowsManifestPath(
    std::string_view path, bool mappedRemoteDrive = false) noexcept;

// Any path whose manifest was deliberately not inspected takes precedence over
// otherwise positive matches: incomplete evidence must never become "active".
[[nodiscard]] RegistrationState registrationStateFromInspection(
    std::size_t expectedMatches, std::size_t otherMatches,
    bool inspectionIncomplete) noexcept;

}  // namespace pose_anchor::status
