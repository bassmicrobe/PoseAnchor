#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include "status_parser.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace pose_anchor::status {
namespace {

constexpr std::uint64_t kMaximumLogBytes = 16ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumMetadataBytes = 2ULL * 1024ULL * 1024ULL;
constexpr int kOpenLogButton = 1001;
constexpr int kOpenReadmeButton = 1002;

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : handle_(handle) {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.release()) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }
    [[nodiscard]] HANDLE release() noexcept {
        return std::exchange(handle_, nullptr);
    }
    void reset(HANDLE replacement = nullptr) noexcept {
        if (*this) CloseHandle(handle_);
        handle_ = replacement;
    }

private:
    HANDLE handle_{};
};

struct FileContents {
    bool exists{};
    bool opened{};
    bool truncated{};
    std::string bytes;
};

struct ProcessInfo {
    bool running{};
    DWORD processId{};
    std::filesystem::path executable;
    std::optional<LocalTimestamp> start;
};

struct RegistrationInfo {
    RegistrationState state{RegistrationState::Unknown};
    std::filesystem::path expectedRoot;
    std::filesystem::path registrationFile;
    std::filesystem::path otherRoot;
    bool skippedRemotePath{};
    bool skippedUnsupportedPath{};
};

enum class EvidenceState {
    Yes,
    No,
    Unknown,
    NotApplicable,
};

struct RuntimeStatus {
    ProcessInfo process;
    std::filesystem::path logPath;
    FileContents log;
    SessionSummary session;
    bool completeSessionCoverage{};
    EvidenceState loaded{EvidenceState::NotApplicable};
    EvidenceState hook{EvidenceState::NotApplicable};
};

struct UiText {
    std::wstring title;
    std::wstring mainInstruction;
    std::wstring content;
    std::wstring details;
    std::wstring footer;
    std::wstring openLog;
    std::wstring openReadme;
    bool warning{};
};

struct DialogContext {
    std::filesystem::path logPath;
    std::filesystem::path readmePath;
    bool logAvailable{};
    bool readmeAvailable{};
    bool japanese{};
};

[[nodiscard]] bool fileExists(const std::filesystem::path& path) noexcept {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

[[nodiscard]] std::wstring utf8ToWide(std::string_view input) {
    if (input.empty()) return {};
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring output(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                            static_cast<int>(input.size()), output.data(), required) != required) {
        return {};
    }
    return output;
}

[[nodiscard]] std::string wideToUtf8(std::wstring_view input) {
    if (input.empty()) return {};
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string output(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
                            static_cast<int>(input.size()), output.data(), required,
                            nullptr, nullptr) != required) {
        return {};
    }
    return output;
}

[[nodiscard]] FileContents readFile(
    const std::filesystem::path& path, std::uint64_t maximumBytes, bool tail) {
    FileContents result;
    result.exists = fileExists(path);
    if (!result.exists) return result;

    UniqueHandle file(CreateFileW(path.c_str(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) return result;
    result.opened = true;

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 0) {
        result.opened = false;
        return result;
    }

    const std::uint64_t unsignedSize = static_cast<std::uint64_t>(size.QuadPart);
    const std::uint64_t wanted = std::min(unsignedSize, maximumBytes);
    std::uint64_t start = 0;
    if (tail && unsignedSize > wanted) {
        start = unsignedSize - wanted;
        result.truncated = true;
    } else if (!tail && unsignedSize > wanted) {
        result.truncated = true;
        result.opened = false;
        return result;
    }

    LARGE_INTEGER offset{};
    offset.QuadPart = static_cast<LONGLONG>(start);
    if (!SetFilePointerEx(file.get(), offset, nullptr, FILE_BEGIN)) {
        result.opened = false;
        return result;
    }

    result.bytes.resize(static_cast<std::size_t>(wanted));
    std::size_t total = 0;
    while (total < result.bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
            result.bytes.size() - total, static_cast<std::size_t>(64 * 1024)));
        DWORD received{};
        if (!ReadFile(file.get(), result.bytes.data() + total, chunk, &received, nullptr)) {
            result.opened = false;
            result.bytes.clear();
            return result;
        }
        if (received == 0) break;
        total += received;
    }
    result.bytes.resize(total);

    if (result.truncated) {
        const std::size_t firstCompleteLine = result.bytes.find('\n');
        if (firstCompleteLine == std::string::npos) result.bytes.clear();
        else result.bytes.erase(0, firstCompleteLine + 1);
    } else if (result.bytes.size() >= 3 &&
               static_cast<unsigned char>(result.bytes[0]) == 0xEF &&
               static_cast<unsigned char>(result.bytes[1]) == 0xBB &&
               static_cast<unsigned char>(result.bytes[2]) == 0xBF) {
        result.bytes.erase(0, 3);
    }
    return result;
}

[[nodiscard]] std::optional<std::wstring> queryRegistryString(
    HKEY root, const wchar_t* subkey, const wchar_t* name, REGSAM view = 0) {
    HKEY rawKey{};
    if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE | view, &rawKey) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    struct KeyCloser {
        HKEY key{};
        ~KeyCloser() { if (key) RegCloseKey(key); }
    } key{rawKey};

    DWORD type{};
    DWORD bytes{};
    if (RegQueryValueExW(key.key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t) ||
        bytes > 64 * 1024) {
        return std::nullopt;
    }
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key.key, name, nullptr, &type,
                         reinterpret_cast<BYTE*>(value.data()), &bytes) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    if (type == REG_EXPAND_SZ) {
        const DWORD required = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
        if (required != 0 && required <= 32768) {
            std::wstring expanded(required, L'\0');
            if (ExpandEnvironmentStringsW(value.c_str(), expanded.data(), required) == required) {
                while (!expanded.empty() && expanded.back() == L'\0') expanded.pop_back();
                value = std::move(expanded);
            }
        }
    }
    return value.empty() ? std::nullopt : std::optional<std::wstring>{std::move(value)};
}

[[nodiscard]] std::optional<std::filesystem::path> localAppDataPath() {
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required == 0 || required > 32768) return std::nullopt;
    std::wstring value(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), required);
    if (written == 0 || written >= required) return std::nullopt;
    value.resize(written);
    return std::filesystem::path(std::move(value));
}

[[nodiscard]] std::optional<std::filesystem::path> steamRootFromRegistry() {
    if (auto path = queryRegistryString(
            HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath")) {
        return std::filesystem::path(std::move(*path));
    }
    constexpr wchar_t uninstallKey[] =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 250820";
    for (const REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
        if (auto path = queryRegistryString(
                HKEY_LOCAL_MACHINE, uninstallKey, L"InstallLocation", view)) {
            std::filesystem::path steamVr(std::move(*path));
            // .../Steam/steamapps/common/SteamVR -> .../Steam
            for (auto current = steamVr; !current.empty(); current = current.parent_path()) {
                if (_wcsicmp(current.filename().c_str(), L"steamapps") == 0) {
                    return current.parent_path();
                }
                if (current == current.root_path()) break;
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::filesystem::path> steamRootFromExecutable(
    const std::filesystem::path& executable) {
    for (auto current = executable.parent_path(); !current.empty(); current = current.parent_path()) {
        if (_wcsicmp(current.filename().c_str(), L"steamapps") == 0) {
            return current.parent_path();
        }
        if (current == current.root_path()) break;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<LocalTimestamp> fileTimeToLocalTimestamp(
    const FILETIME& fileTime) noexcept {
    SYSTEMTIME utc{};
    SYSTEMTIME local{};
    if (!FileTimeToSystemTime(&fileTime, &utc) ||
        !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
        return std::nullopt;
    }
    LocalTimestamp result{
        local.wYear, local.wMonth, local.wDay, local.wHour,
        local.wMinute, local.wSecond, local.wMilliseconds,
    };
    return result.valid() ? std::optional<LocalTimestamp>{result} : std::nullopt;
}

[[nodiscard]] ProcessInfo findVrServer() {
    ProcessInfo result;
    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) return result;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) return result;

    std::uint64_t newestCreation = 0;
    bool inaccessibleMatch = false;
    do {
        if (_wcsicmp(entry.szExeFile, L"vrserver.exe") != 0) continue;
        UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                         FALSE, entry.th32ProcessID));
        if (!process) {
            inaccessibleMatch = inaccessibleMatch || GetLastError() == ERROR_ACCESS_DENIED;
            continue;
        }
        if (WaitForSingleObject(process.get(), 0) != WAIT_TIMEOUT) continue;
        result.running = true;

        FILETIME created{};
        FILETIME exited{};
        FILETIME kernel{};
        FILETIME user{};
        if (!GetProcessTimes(process.get(), &created, &exited, &kernel, &user)) continue;
        ULARGE_INTEGER packed{};
        packed.LowPart = created.dwLowDateTime;
        packed.HighPart = created.dwHighDateTime;
        if (packed.QuadPart < newestCreation) continue;

        newestCreation = packed.QuadPart;
        result.processId = entry.th32ProcessID;
        result.start = fileTimeToLocalTimestamp(created);

        std::wstring path(32768, L'\0');
        DWORD length = static_cast<DWORD>(path.size());
        if (QueryFullProcessImageNameW(process.get(), 0, path.data(), &length)) {
            path.resize(length);
            result.executable = std::filesystem::path(std::move(path));
        }
    } while (Process32NextW(snapshot.get(), &entry));
    if (!result.running && inaccessibleMatch) result.running = true;
    return result;
}

[[nodiscard]] std::filesystem::path executablePath() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    path.resize(length);
    return std::filesystem::path(std::move(path));
}

[[nodiscard]] ManifestPathKind classifyManifestCandidate(
    const std::filesystem::path& candidate);

[[nodiscard]] std::filesystem::path findDriverRoot(
    const std::filesystem::path& executable) {
    auto current = executable.parent_path();
    for (int level = 0; level < 4 && !current.empty(); ++level) {
        // Never probe a manifest while walking a network or unsupported path.
        // inspectRegistration() will surface this as incomplete evidence.
        if (classifyManifestCandidate(current) != ManifestPathKind::Local) {
            return executable.parent_path();
        }
        if (fileExists(current / L"driver.vrdrivermanifest")) return current;
        if (current == current.root_path()) break;
        current = current.parent_path();
    }
    return executable.parent_path();
}

[[nodiscard]] std::wstring normalizedFullPath(const std::filesystem::path& input) {
    if (input.empty()) return {};
    const DWORD required = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
    std::wstring output;
    if (required != 0 && required <= 32768) {
        output.resize(required, L'\0');
        const DWORD written = GetFullPathNameW(input.c_str(), required, output.data(), nullptr);
        if (written != 0 && written < required) output.resize(written);
        else output = input.native();
    } else {
        output = input.native();
    }
    std::replace(output.begin(), output.end(), L'/', L'\\');
    while (output.size() > 3 && output.back() == L'\\') output.pop_back();
    return output;
}

[[nodiscard]] bool equalWindowsPath(
    const std::filesystem::path& left, const std::filesystem::path& right) {
    const std::wstring normalizedLeft = normalizedFullPath(left);
    const std::wstring normalizedRight = normalizedFullPath(right);
    if (normalizedLeft.empty() || normalizedRight.empty()) return false;
    return CompareStringOrdinal(normalizedLeft.data(), static_cast<int>(normalizedLeft.size()),
                                normalizedRight.data(), static_cast<int>(normalizedRight.size()),
                                TRUE) == CSTR_EQUAL;
}

[[nodiscard]] bool containsOrdinalIgnoreCase(
    std::wstring_view text, std::wstring_view wanted) noexcept {
    if (wanted.empty() || wanted.size() > text.size()) return false;
    for (std::size_t offset = 0; offset + wanted.size() <= text.size(); ++offset) {
        if (CompareStringOrdinal(text.data() + offset, static_cast<int>(wanted.size()),
                                 wanted.data(), static_cast<int>(wanted.size()),
                                 TRUE) == CSTR_EQUAL) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::optional<wchar_t> driveLetter(
    std::wstring_view path) noexcept {
    const auto validLetter = [](wchar_t value) {
        return (value >= L'A' && value <= L'Z') || (value >= L'a' && value <= L'z');
    };
    if (path.size() >= 2 && validLetter(path[0]) && path[1] == L':') return path[0];
    if (path.size() >= 6 && path[0] == L'\\' && path[1] == L'\\' &&
        path[2] == L'?' && path[3] == L'\\' && validLetter(path[4]) &&
        path[5] == L':') {
        return path[4];
    }
    return std::nullopt;
}

[[nodiscard]] ManifestPathKind classifyManifestCandidate(
    const std::filesystem::path& candidate) {
    const std::string original = wideToUtf8(candidate.native());
    if (original.empty() && !candidate.empty()) return ManifestPathKind::Unsupported;
    ManifestPathKind kind = classifyWindowsManifestPath(original);
    if (kind != ManifestPathKind::Local) return kind;

    const std::wstring normalized = normalizedFullPath(candidate);
    const std::string normalizedUtf8 = wideToUtf8(normalized);
    if (normalizedUtf8.empty()) return ManifestPathKind::Unsupported;
    kind = classifyWindowsManifestPath(normalizedUtf8);
    if (kind != ManifestPathKind::Local) return kind;

    const auto letter = driveLetter(normalized);
    if (!letter) return ManifestPathKind::Local;

    const std::array<wchar_t, 3> deviceName{*letter, L':', L'\0'};
    std::array<wchar_t, 4096> target{};
    const DWORD targetLength = QueryDosDeviceW(
        deviceName.data(), target.data(), static_cast<DWORD>(target.size()));
    if (targetLength != 0) {
        const std::wstring_view mapping(target.data(), targetLength);
        if (containsOrdinalIgnoreCase(mapping, L"\\device\\mup\\") ||
            containsOrdinalIgnoreCase(mapping, L"lanmanredirector") ||
            containsOrdinalIgnoreCase(mapping, L"webdavredirector") ||
            containsOrdinalIgnoreCase(mapping, L"\\device\\rdpdr\\") ||
            containsOrdinalIgnoreCase(mapping, L"\\??\\unc\\")) {
            return classifyWindowsManifestPath(normalizedUtf8, true);
        }
    }

    const std::array<wchar_t, 4> root{*letter, L':', L'\\', L'\0'};
    const UINT driveType = GetDriveTypeW(root.data());
    if (driveType == DRIVE_REMOTE) {
        return classifyWindowsManifestPath(normalizedUtf8, true);
    }
    if (driveType == DRIVE_UNKNOWN || driveType == DRIVE_NO_ROOT_DIR) {
        return ManifestPathKind::Unsupported;
    }
    return ManifestPathKind::Local;
}

[[nodiscard]] bool isPoseAnchorDriverRoot(const std::filesystem::path& root) {
    const FileContents manifest = readFile(
        root / L"driver.vrdrivermanifest", kMaximumMetadataBytes, false);
    if (!manifest.opened || manifest.truncated) return false;
    const JsonStringResult name = findJsonString(manifest.bytes, "name");
    return name.status == JsonLookupStatus::Found && name.value == "pose_anchor";
}

[[nodiscard]] RegistrationInfo inspectRegistration(
    const std::filesystem::path& driverRoot) {
    RegistrationInfo result;
    result.expectedRoot = driverRoot;

    const ManifestPathKind expectedKind = classifyManifestCandidate(driverRoot);
    if (expectedKind != ManifestPathKind::Local) {
        result.state = RegistrationState::InspectionIncomplete;
        result.skippedRemotePath = expectedKind == ManifestPathKind::Remote;
        result.skippedUnsupportedPath = expectedKind == ManifestPathKind::Unsupported;
        return result;
    }
    if (!isPoseAnchorDriverRoot(driverRoot) ||
        !fileExists(driverRoot / L"bin" / L"win64" / L"driver_pose_anchor.dll")) {
        result.state = RegistrationState::BrokenInstallation;
        return result;
    }

    const auto local = localAppDataPath();
    if (!local) {
        result.state = RegistrationState::Unknown;
        return result;
    }
    result.registrationFile = *local / L"openvr" / L"openvrpaths.vrpath";
    const FileContents registration = readFile(
        result.registrationFile, kMaximumMetadataBytes, false);
    if (!registration.exists) {
        result.state = RegistrationState::NotRegistered;
        return result;
    }
    if (!registration.opened || registration.truncated) {
        result.state = RegistrationState::Unknown;
        return result;
    }

    const JsonStringArrayResult drivers = findJsonStringArray(
        registration.bytes, "external_drivers");
    if (drivers.status == JsonLookupStatus::Missing) {
        result.state = RegistrationState::NotRegistered;
        return result;
    }
    if (drivers.status == JsonLookupStatus::Malformed) {
        result.state = RegistrationState::Unknown;
        return result;
    }

    std::size_t expectedMatches = 0;
    std::size_t otherMatches = 0;
    bool inspectionIncomplete = false;
    for (const std::string& utf8Path : drivers.values) {
        ManifestPathKind pathKind = classifyWindowsManifestPath(utf8Path);
        if (pathKind != ManifestPathKind::Local) {
            inspectionIncomplete = true;
            result.skippedRemotePath = result.skippedRemotePath ||
                pathKind == ManifestPathKind::Remote;
            result.skippedUnsupportedPath = result.skippedUnsupportedPath ||
                pathKind == ManifestPathKind::Unsupported;
            continue;
        }
        const std::wstring widePath = utf8ToWide(utf8Path);
        if (widePath.empty() && !utf8Path.empty()) {
            inspectionIncomplete = true;
            result.skippedUnsupportedPath = true;
            continue;
        }
        const std::filesystem::path candidate(widePath);
        pathKind = classifyManifestCandidate(candidate);
        if (pathKind != ManifestPathKind::Local) {
            inspectionIncomplete = true;
            result.skippedRemotePath = result.skippedRemotePath ||
                pathKind == ManifestPathKind::Remote;
            result.skippedUnsupportedPath = result.skippedUnsupportedPath ||
                pathKind == ManifestPathKind::Unsupported;
            continue;
        }
        if (equalWindowsPath(candidate, driverRoot)) {
            ++expectedMatches;
            continue;
        }
        if (isPoseAnchorDriverRoot(candidate)) {
            ++otherMatches;
            if (result.otherRoot.empty()) result.otherRoot = candidate;
        }
    }
    result.state = registrationStateFromInspection(
        expectedMatches, otherMatches, inspectionIncomplete);
    return result;
}

[[nodiscard]] RuntimeStatus inspectRuntime() {
    RuntimeStatus result;
    result.process = findVrServer();
    std::optional<std::filesystem::path> steamRoot;
    if (!result.process.executable.empty()) {
        steamRoot = steamRootFromExecutable(result.process.executable);
    }
    if (!steamRoot) steamRoot = steamRootFromRegistry();
    if (steamRoot) result.logPath = *steamRoot / L"logs" / L"vrserver.txt";
    // Keep the existing log available to the explicit "Open log" action while
    // SteamVR is stopped, but never use it as evidence for current runtime state.
    if (!result.process.running) return result;
    if (result.logPath.empty()) return result;

    result.log = readFile(result.logPath, kMaximumLogBytes, true);
    if (!result.log.opened || !result.process.start) return result;

    ParseOptions options;
    options.sessionStart = result.process.start;
    options.maxRecentEvents = 5;
    result.session = parseVrServerLog(result.log.bytes, options);
    result.completeSessionCoverage = !result.log.truncated ||
        (result.session.oldestTimestamp &&
         *result.session.oldestTimestamp <= *result.process.start);

    if (result.session.loaded) {
        result.loaded = EvidenceState::Yes;
    } else if (result.session.sawUnloadLine ||
               (result.completeSessionCoverage && result.session.sawCurrentSessionLine)) {
        result.loaded = EvidenceState::No;
    } else {
        result.loaded = EvidenceState::Unknown;
    }

    if (result.loaded == EvidenceState::Yes) {
        if (result.session.hookInstalled) result.hook = EvidenceState::Yes;
        else if (result.session.filterDisabled || result.session.hookFailure ||
                 result.completeSessionCoverage) result.hook = EvidenceState::No;
        else result.hook = EvidenceState::Unknown;
    } else if (result.loaded == EvidenceState::No) {
        result.hook = EvidenceState::NotApplicable;
    } else {
        result.hook = EvidenceState::Unknown;
    }
    return result;
}

[[nodiscard]] bool isJapaneseUi() noexcept {
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_JAPANESE;
}

[[nodiscard]] std::wstring formatTimestamp(const LocalTimestamp& value, bool date) {
    std::wostringstream stream;
    stream << std::setfill(L'0');
    if (date) {
        stream << std::setw(4) << value.year << L'-' << std::setw(2) << value.month
               << L'-' << std::setw(2) << value.day << L' ';
    }
    stream << std::setw(2) << value.hour << L':' << std::setw(2) << value.minute
           << L':' << std::setw(2) << value.second;
    if (!date) stream << L'.' << std::setw(3) << value.millisecond;
    return stream.str();
}

[[nodiscard]] std::wstring registrationLabel(
    const RegistrationInfo& registration, bool japanese) {
    switch (registration.state) {
        case RegistrationState::Registered:
            return japanese ? L"登録済み" : L"Registered";
        case RegistrationState::NotRegistered:
            return japanese ? L"未登録" : L"Not registered";
        case RegistrationState::RegisteredElsewhere:
            return japanese ? L"別のコピーが登録されています" : L"A different copy is registered";
        case RegistrationState::ConflictingRegistrations:
            return japanese ? L"重複または競合する登録があります"
                            : L"Duplicate or conflicting registrations";
        case RegistrationState::InspectionIncomplete:
            return japanese ? L"一部の登録先を安全に確認できません"
                            : L"Some registration paths were not safely inspected";
        case RegistrationState::BrokenInstallation:
            return japanese ? L"インストールが不完全です" : L"Installation is incomplete";
        case RegistrationState::Unknown:
            return japanese ? L"確認できません" : L"Could not verify";
    }
    return {};
}

[[nodiscard]] std::wstring evidenceLabel(EvidenceState state, bool japanese) {
    switch (state) {
        case EvidenceState::Yes: return japanese ? L"はい" : L"Yes";
        case EvidenceState::No: return japanese ? L"いいえ" : L"No";
        case EvidenceState::Unknown: return japanese ? L"確認できません" : L"Could not verify";
        case EvidenceState::NotApplicable: return L"—";
    }
    return {};
}

void appendTrackerSummary(std::wostringstream& stream, const RuntimeStatus& runtime,
                          bool japanese) {
    if (!runtime.process.running) {
        stream << (japanese
            ? L"Vive Tracker: —（SteamVR停止中。以前のログは判定に使用していません）\n"
            : L"Vive Tracker: — (SteamVR is stopped; previous logs are not used)\n");
        return;
    }
    if (runtime.loaded != EvidenceState::Yes) {
        stream << (japanese ? L"Vive Tracker: 確認できません\n"
                            : L"Vive Tracker: Could not verify\n");
        return;
    }
    stream << L"Vive Tracker: " << runtime.session.trackers.size();
    if (runtime.session.trackersTruncated) {
        stream << (japanese ? L"+（表示上限）" : L"+ (display limit)");
    }
    if (runtime.session.trackers.empty()) {
        stream << (japanese ? L"（現セッションで未認識）\n"
                            : L" (none recognized in this session)\n");
        return;
    }
    stream << L'\n';
    for (const TrackerInfo& tracker : runtime.session.trackers) {
        stream << L"  • " << utf8ToWide(tracker.serial);
        if (!tracker.model.empty()) stream << L" — " << utf8ToWide(tracker.model);
        stream << L'\n';
    }
}

void appendEventSummary(std::wostringstream& stream, const RuntimeStatus& runtime,
                        bool japanese) {
    if (!runtime.process.running || runtime.loaded != EvidenceState::Yes) {
        stream << (japanese ? L"イベント: —\n" : L"Events: —\n");
        return;
    }

    const bool partial = !runtime.completeSessionCoverage;
    stream << (japanese ? L"ログで確認できたイベント: " : L"Events observed in log: ")
           << L"Hold " << runtime.session.holdTransitions
           << L" / Recovery " << runtime.session.recoveryTransitions
           << L" / Lost " << runtime.session.lostTransitions;
    if (partial) stream << (japanese ? L" 以上" : L" or more");
    stream << L'\n';

    if (runtime.session.recentEvents.empty()) {
        stream << (japanese ? L"  直近イベントなし\n" : L"  No recent events\n");
        return;
    }
    for (const TransitionEvent& event : runtime.session.recentEvents) {
        stream << L"  • " << formatTimestamp(event.timestamp, false) << L"  "
               << utf8ToWide(event.serial) << L"  " << utf8ToWide(event.fromState)
               << L" → " << utf8ToWide(event.toState);
        if (event.suppressed != 0) {
            stream << (japanese ? L"（省略分 " : L" (suppressed ")
                   << event.suppressed << L')';
        }
        stream << L'\n';
    }
}

[[nodiscard]] UiText buildUiText(const RegistrationInfo& registration,
                                 const RuntimeStatus& runtime, bool japanese) {
    UiText text;
    text.title = japanese ? L"PoseAnchor ステータス" : L"PoseAnchor Status";
    text.openLog = japanese ? L"ログを開く" : L"Open log";
    text.openReadme = japanese ? L"READMEを開く" : L"Open README";
    text.footer = japanese
        ? L"起動時点のスナップショットです。閉じるとプロセスは残りません。"
        : L"Snapshot at launch; no process remains after this window is closed.";

    const bool active = registration.state == RegistrationState::Registered &&
        runtime.process.running && runtime.loaded == EvidenceState::Yes &&
        runtime.hook == EvidenceState::Yes;
    if (active) {
        text.mainInstruction = japanese ? L"PoseAnchor は有効です"
                                        : L"PoseAnchor is active";
    } else if (!runtime.process.running &&
               registration.state == RegistrationState::Registered) {
        text.mainInstruction = japanese ? L"SteamVR は停止しています"
                                        : L"SteamVR is stopped";
    } else {
        text.mainInstruction = japanese ? L"PoseAnchor の確認が必要です"
                                        : L"PoseAnchor needs attention";
        text.warning = true;
    }

    std::wostringstream content;
    content << (japanese ? L"SteamVR登録: " : L"SteamVR registration: ")
            << registrationLabel(registration, japanese) << L'\n';
    if (registration.state == RegistrationState::InspectionIncomplete) {
        if (registration.skippedRemotePath) {
            content << (japanese
                ? L"  ネットワーク上の登録先は開かず、稼働判定から除外しました。\n"
                : L"  Network registration paths were not opened and cannot prove active status.\n");
        }
        if (registration.skippedUnsupportedPath) {
            content << (japanese
                ? L"  読み取れない登録先があるため、登録状態を確定していません。\n"
                : L"  An unreadable registration path kept the result unverified.\n");
        }
    }

    if (runtime.process.running) {
        content << (japanese ? L"SteamVR: 実行中" : L"SteamVR: Running");
        if (runtime.process.processId != 0) {
            content << L" (PID " << runtime.process.processId;
            if (runtime.process.start) {
                content << (japanese ? L"、開始 " : L", started ")
                        << formatTimestamp(*runtime.process.start, true);
            }
            content << L')';
        }
        content << L'\n';
        content << (japanese ? L"ドライバー読込: " : L"Driver loaded: ")
                << evidenceLabel(runtime.loaded, japanese);
        if (runtime.loaded == EvidenceState::Yes && !runtime.session.version.empty()) {
            content << L" (v" << utf8ToWide(runtime.session.version) << L')';
        }
        content << L'\n';
        content << (japanese ? L"Pose hook: " : L"Pose hook: ")
                << evidenceLabel(runtime.hook, japanese);
        if (runtime.session.filterDisabled) {
            content << (japanese ? L"（設定で無効）" : L" (disabled in settings)");
        } else if (runtime.session.hookFailure) {
            content << (japanese ? L"（導入失敗をログで検出）" : L" (installation failure in log)");
        }
        content << L'\n';
    } else {
        content << (japanese ? L"SteamVR: 停止中\n" : L"SteamVR: Stopped\n")
                << (japanese ? L"ドライバー読込: —\nPose hook: —\n"
                             : L"Driver loaded: —\nPose hook: —\n");
    }
    content << L'\n';
    appendTrackerSummary(content, runtime, japanese);
    content << L'\n';
    appendEventSummary(content, runtime, japanese);

    if (runtime.process.running) {
        content << L'\n';
        if (!runtime.log.exists) {
            content << (japanese ? L"ログ: vrserver.txt が見つかりません"
                                 : L"Log: vrserver.txt was not found");
        } else if (!runtime.log.opened) {
            content << (japanese ? L"ログ: 読み取れません"
                                 : L"Log: Could not read vrserver.txt");
        } else if (!runtime.process.start) {
            content << (japanese ? L"ログ: プロセス開始時刻を取得できないため現セッションを確認できません"
                                 : L"Log: Current session cannot be verified because process start time is unavailable");
        } else if (!runtime.session.sawCurrentSessionLine) {
            content << (japanese ? L"ログ: 現セッションの記録なし（古い記録は判定に不使用）"
                                 : L"Log: No current-session records (older records were not used)");
        } else if (!runtime.completeSessionCoverage) {
            content << (japanese ? L"ログ: 現セッションの一部のみ取得"
                                 : L"Log: Only part of the current session was available");
        } else {
            content << (japanese ? L"ログ: 現セッションを確認済み"
                                 : L"Log: Current session verified");
        }
    }
    text.content = content.str();

    std::wostringstream details;
    details << (japanese ? L"確認対象:\n" : L"Inspected paths:\n")
            << registration.expectedRoot.native();
    if (!registration.registrationFile.empty()) {
        details << L"\n" << registration.registrationFile.native();
    }
    if (!runtime.logPath.empty()) details << L"\n" << runtime.logPath.native();
    if (!registration.otherRoot.empty()) {
        details << (japanese ? L"\n別の登録先: " : L"\nOther registered copy: ")
                << registration.otherRoot.native();
    }
    text.details = details.str();
    return text;
}

void showOpenError(HWND owner, bool japanese) {
    MessageBoxW(owner,
                japanese ? L"ファイルを開けませんでした。ファイルが移動または削除されていないか確認してください。"
                         : L"The file could not be opened. Check whether it was moved or deleted.",
                L"PoseAnchor", MB_OK | MB_ICONERROR);
}

void openPath(HWND owner, const std::filesystem::path& path, bool japanese) {
    if (!fileExists(path)) {
        showOpenError(owner, japanese);
        return;
    }
    std::wstring systemDirectory(MAX_PATH, L'\0');
    const UINT length = GetSystemDirectoryW(
        systemDirectory.data(), static_cast<UINT>(systemDirectory.size()));
    if (length == 0 || length >= systemDirectory.size() ||
        path.native().find(L'"') != std::wstring::npos) {
        showOpenError(owner, japanese);
        return;
    }
    systemDirectory.resize(length);
    const std::filesystem::path notepad =
        std::filesystem::path(std::move(systemDirectory)) / L"notepad.exe";
    const std::wstring parameters = L"\"" + path.native() + L"\"";
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
        owner, L"open", notepad.c_str(), parameters.c_str(),
        path.parent_path().c_str(), SW_SHOWNORMAL));
    if (result <= 32) showOpenError(owner, japanese);
}

HRESULT CALLBACK taskDialogCallback(HWND window, UINT notification,
                                    WPARAM parameter, LPARAM, LONG_PTR data) {
    auto* context = reinterpret_cast<DialogContext*>(data);
    if (!context) return S_OK;
    if (notification == TDN_CREATED) {
        SendMessageW(window, TDM_ENABLE_BUTTON, kOpenLogButton, context->logAvailable);
        SendMessageW(window, TDM_ENABLE_BUTTON, kOpenReadmeButton, context->readmeAvailable);
    } else if (notification == TDN_BUTTON_CLICKED) {
        if (parameter == kOpenLogButton) {
            try {
                openPath(window, context->logPath, context->japanese);
            } catch (...) {
                showOpenError(window, context->japanese);
            }
            return S_FALSE;
        }
        if (parameter == kOpenReadmeButton) {
            try {
                openPath(window, context->readmePath, context->japanese);
            } catch (...) {
                showOpenError(window, context->japanese);
            }
            return S_FALSE;
        }
    }
    return S_OK;
}

int showStatusDialog(const UiText& text, DialogContext& context) {
    const std::array<TASKDIALOG_BUTTON, 2> buttons{{
        {kOpenLogButton, text.openLog.c_str()},
        {kOpenReadmeButton, text.openReadme.c_str()},
    }};

    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hInstance = GetModuleHandleW(nullptr);
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT |
                     TDF_EXPAND_FOOTER_AREA;
    config.dwCommonButtons = TDCBF_CLOSE_BUTTON;
    config.pszWindowTitle = text.title.c_str();
    config.pszMainIcon = text.warning ? TD_WARNING_ICON : TD_INFORMATION_ICON;
    config.pszMainInstruction = text.mainInstruction.c_str();
    config.pszContent = text.content.c_str();
    config.cButtons = static_cast<UINT>(buttons.size());
    config.pButtons = buttons.data();
    config.nDefaultButton = IDCLOSE;
    config.pszExpandedInformation = text.details.c_str();
    config.pszExpandedControlText = context.japanese ? L"確認したパスを隠す"
                                                     : L"Hide inspected paths";
    config.pszCollapsedControlText = context.japanese ? L"確認したパスを表示"
                                                      : L"Show inspected paths";
    config.pszFooter = text.footer.c_str();
    config.pfCallback = taskDialogCallback;
    config.lpCallbackData = reinterpret_cast<LONG_PTR>(&context);

    int selected{};
    const HRESULT result = TaskDialogIndirect(&config, &selected, nullptr, nullptr);
    if (SUCCEEDED(result)) return EXIT_SUCCESS;

    // This fallback is only for systems where the standard Task Dialog API is
    // unavailable or damaged. Status remains readable and the process still exits.
    const std::wstring fallback = text.mainInstruction + L"\n\n" + text.content;
    MessageBoxW(nullptr, fallback.c_str(), text.title.c_str(),
                MB_OK | (text.warning ? MB_ICONWARNING : MB_ICONINFORMATION));
    return EXIT_FAILURE;
}

}  // namespace
}  // namespace pose_anchor::status

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    using namespace pose_anchor::status;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const bool japanese = isJapaneseUi();
    try {
        const std::filesystem::path executable = executablePath();
        const std::filesystem::path driverRoot = findDriverRoot(executable);
        const RegistrationInfo registration = inspectRegistration(driverRoot);
        const RuntimeStatus runtime = inspectRuntime();
        const UiText text = buildUiText(registration, runtime, japanese);

        DialogContext context;
        context.logPath = runtime.logPath;
        context.readmePath = driverRoot / L"README.md";
        context.logAvailable = fileExists(context.logPath);
        context.readmeAvailable = fileExists(context.readmePath);
        context.japanese = japanese;
        return showStatusDialog(text, context);
    } catch (...) {
        MessageBoxW(nullptr,
                    japanese ? L"ステータス情報の読み取り中に予期しないエラーが発生しました。"
                             : L"An unexpected error occurred while reading status information.",
                    japanese ? L"PoseAnchor ステータス" : L"PoseAnchor Status",
                    MB_OK | MB_ICONERROR);
        return EXIT_FAILURE;
    }
}
