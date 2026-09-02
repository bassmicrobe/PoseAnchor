#include "status_parser.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <limits>
#include <tuple>
#include <utility>

namespace pose_anchor::status {
namespace {

constexpr std::array<std::string_view, 12> kMonths{
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

constexpr std::string_view kPoseAnchorTag = "[PoseAnchor] ";
constexpr std::size_t kMaximumTrackers = 64;
constexpr std::size_t kMaximumIdentityLength = 256;
constexpr std::size_t kMaximumStateNameLength = 32;
constexpr std::size_t kMaximumJsonDepth = 64;

bool parseUnsigned(std::string_view text, int& value) noexcept {
    if (text.empty()) return false;
    int parsed{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size()) return false;
    value = parsed;
    return true;
}

bool parseUint32(std::string_view text, int base, std::uint32_t& value) noexcept {
    if (text.empty()) return false;
    std::uint32_t parsed{};
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), parsed, base);
    if (error != std::errc{} || end != text.data() + text.size()) return false;
    value = parsed;
    return true;
}

std::string_view trim(std::string_view value) noexcept {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

std::string_view tokenValue(std::string_view message, std::string_view key) noexcept {
    const std::size_t position = message.find(key);
    if (position == std::string_view::npos) return {};
    const std::size_t start = position + key.size();
    const std::size_t end = message.find_first_of(" \t\r\n", start);
    return message.substr(start, end == std::string_view::npos ? message.size() - start
                                                               : end - start);
}

void addSaturating(std::size_t& destination, std::uint32_t count) noexcept {
    if (destination > std::numeric_limits<std::size_t>::max() - count) {
        destination = std::numeric_limits<std::size_t>::max();
    } else {
        destination += count;
    }
}

bool startsWith(std::string_view value, std::string_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::optional<TrackerInfo> parseTracker(std::string_view message) {
    constexpr std::string_view prefix = "identified Vive Tracker ";
    if (!startsWith(message, prefix)) return std::nullopt;

    message.remove_prefix(prefix.size());
    const std::size_t modelStart = message.find(" (");
    if (modelStart == std::string_view::npos || modelStart == 0 ||
        modelStart > kMaximumIdentityLength) {
        return std::nullopt;
    }
    const std::size_t modelEnd = message.find(')', modelStart + 2);
    if (modelEnd == std::string_view::npos || modelEnd == modelStart + 2 ||
        modelEnd - modelStart - 2 > kMaximumIdentityLength) {
        return std::nullopt;
    }

    TrackerInfo result;
    result.serial.assign(message.substr(0, modelStart));
    result.model.assign(message.substr(modelStart + 2, modelEnd - modelStart - 2));
    return result;
}

std::optional<TransitionEvent> parseTransition(
    std::string_view message, const LocalTimestamp& timestamp) {
    constexpr std::string_view prefix = "tracker ";
    if (!startsWith(message, prefix)) return std::nullopt;
    message.remove_prefix(prefix.size());

    const std::size_t serialEnd = message.find(' ');
    if (serialEnd == std::string_view::npos || serialEnd == 0 ||
        serialEnd > kMaximumIdentityLength) {
        return std::nullopt;
    }

    TransitionEvent event;
    event.timestamp = timestamp;
    event.serial.assign(message.substr(0, serialEnd));
    message.remove_prefix(serialEnd + 1);

    const std::size_t arrow = message.find(" -> ");
    if (arrow == std::string_view::npos || arrow == 0 ||
        arrow > kMaximumStateNameLength) {
        return std::nullopt;
    }
    event.fromState.assign(message.substr(0, arrow));
    message.remove_prefix(arrow + 4);

    const std::size_t toEnd = message.find(' ');
    if (toEnd == std::string_view::npos || toEnd == 0 ||
        toEnd > kMaximumStateNameLength) {
        return std::nullopt;
    }
    event.toState.assign(message.substr(0, toEnd));

    std::string_view reasons = tokenValue(message, "reasons=0x");
    if (reasons.empty() || !parseUint32(reasons, 16, event.reasons)) return std::nullopt;

    const std::string_view suppressed = tokenValue(message, "suppressed=");
    if (!suppressed.empty()) {
        std::uint32_t parsed{};
        if (parseUint32(suppressed, 10, parsed)) event.suppressed = parsed;
    }
    return event;
}

class JsonCursor {
public:
    explicit JsonCursor(std::string_view input) : input_(input) {}

    [[nodiscard]] std::size_t position() const noexcept { return position_; }
    void position(std::size_t value) noexcept { position_ = std::min(value, input_.size()); }

    void skipWhitespace() noexcept {
        while (position_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[position_])) != 0) {
            ++position_;
        }
    }

    [[nodiscard]] bool consume(char expected) noexcept {
        skipWhitespace();
        if (position_ >= input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    [[nodiscard]] bool atEnd() noexcept {
        skipWhitespace();
        return position_ == input_.size();
    }

    [[nodiscard]] std::optional<std::string> parseString() {
        skipWhitespace();
        if (position_ >= input_.size() || input_[position_] != '"') return std::nullopt;
        ++position_;

        std::string output;
        while (position_ < input_.size()) {
            const unsigned char value = static_cast<unsigned char>(input_[position_++]);
            if (value == '"') return output;
            if (value < 0x20) return std::nullopt;
            if (value != '\\') {
                output.push_back(static_cast<char>(value));
                continue;
            }
            if (position_ >= input_.size()) return std::nullopt;
            const char escaped = input_[position_++];
            switch (escaped) {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case 'u': {
                    std::uint32_t codePoint{};
                    if (!parseHexQuad(codePoint)) return std::nullopt;
                    if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
                        if (position_ + 2 > input_.size() || input_[position_] != '\\' ||
                            input_[position_ + 1] != 'u') {
                            return std::nullopt;
                        }
                        position_ += 2;
                        std::uint32_t low{};
                        if (!parseHexQuad(low) || low < 0xDC00 || low > 0xDFFF) {
                            return std::nullopt;
                        }
                        codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                    } else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
                        return std::nullopt;
                    }
                    appendUtf8(output, codePoint);
                    break;
                }
                default: return std::nullopt;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool skipValue(std::size_t depth = 0) {
        skipWhitespace();
        if (position_ >= input_.size()) return false;
        const char value = input_[position_];
        if (value == '"') return parseString().has_value();
        if (value == '{') {
            return depth < kMaximumJsonDepth && skipObject(depth + 1);
        }
        if (value == '[') {
            return depth < kMaximumJsonDepth && skipArray(depth + 1);
        }
        if (startsWithAt("true")) {
            position_ += 4;
            return true;
        }
        if (startsWithAt("false")) {
            position_ += 5;
            return true;
        }
        if (startsWithAt("null")) {
            position_ += 4;
            return true;
        }
        return skipNumber();
    }

    [[nodiscard]] std::string_view remaining() const noexcept {
        return input_.substr(position_);
    }

private:
    [[nodiscard]] bool parseHexQuad(std::uint32_t& value) noexcept {
        if (position_ + 4 > input_.size()) return false;
        value = 0;
        for (int index = 0; index < 4; ++index) {
            const unsigned char digit = static_cast<unsigned char>(input_[position_++]);
            value <<= 4;
            if (digit >= '0' && digit <= '9') value += digit - '0';
            else if (digit >= 'a' && digit <= 'f') value += digit - 'a' + 10;
            else if (digit >= 'A' && digit <= 'F') value += digit - 'A' + 10;
            else return false;
        }
        return true;
    }

    static void appendUtf8(std::string& output, std::uint32_t codePoint) {
        if (codePoint <= 0x7F) {
            output.push_back(static_cast<char>(codePoint));
        } else if (codePoint <= 0x7FF) {
            output.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else if (codePoint <= 0xFFFF) {
            output.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else {
            output.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
    }

    [[nodiscard]] bool startsWithAt(std::string_view text) const noexcept {
        return position_ + text.size() <= input_.size() &&
               input_.substr(position_, text.size()) == text;
    }

    [[nodiscard]] bool skipNumber() noexcept {
        const std::size_t start = position_;
        if (position_ < input_.size() && input_[position_] == '-') ++position_;
        if (position_ >= input_.size()) return false;
        if (input_[position_] == '0') {
            ++position_;
        } else {
            if (!std::isdigit(static_cast<unsigned char>(input_[position_]))) return false;
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                ++position_;
            }
        }
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            const std::size_t fraction = position_;
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                ++position_;
            }
            if (fraction == position_) return false;
        }
        if (position_ < input_.size() &&
            (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() &&
                (input_[position_] == '+' || input_[position_] == '-')) {
                ++position_;
            }
            const std::size_t exponent = position_;
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                ++position_;
            }
            if (exponent == position_) return false;
        }
        return position_ > start;
    }

    [[nodiscard]] bool skipArray(std::size_t depth) {
        if (!consume('[')) return false;
        skipWhitespace();
        if (consume(']')) return true;
        while (true) {
            if (!skipValue(depth)) return false;
            skipWhitespace();
            if (consume(']')) return true;
            if (!consume(',')) return false;
        }
    }

    [[nodiscard]] bool skipObject(std::size_t depth) {
        if (!consume('{')) return false;
        skipWhitespace();
        if (consume('}')) return true;
        while (true) {
            if (!parseString().has_value() || !consume(':') ||
                !skipValue(depth)) {
                return false;
            }
            skipWhitespace();
            if (consume('}')) return true;
            if (!consume(',')) return false;
        }
    }

    std::string_view input_;
    std::size_t position_{};
};

template <typename FoundValue, typename ParseWanted>
auto findTopLevelMember(std::string_view json, std::string_view wanted,
                        ParseWanted&& parseWanted) -> std::pair<JsonLookupStatus, FoundValue> {
    JsonCursor cursor(json);
    FoundValue result{};
    if (!cursor.consume('{')) return {JsonLookupStatus::Malformed, {}};
    cursor.skipWhitespace();
    if (cursor.consume('}')) {
        return cursor.atEnd() ? std::pair{JsonLookupStatus::Missing, result}
                              : std::pair{JsonLookupStatus::Malformed, FoundValue{}};
    }

    bool found = false;
    while (true) {
        const auto key = cursor.parseString();
        if (!key || !cursor.consume(':')) return {JsonLookupStatus::Malformed, {}};
        if (*key == wanted) {
            if (found || !parseWanted(cursor, result)) return {JsonLookupStatus::Malformed, {}};
            found = true;
        } else if (!cursor.skipValue()) {
            return {JsonLookupStatus::Malformed, {}};
        }

        cursor.skipWhitespace();
        if (cursor.consume('}')) break;
        if (!cursor.consume(',')) return {JsonLookupStatus::Malformed, {}};
    }
    if (!cursor.atEnd()) return {JsonLookupStatus::Malformed, {}};
    return {found ? JsonLookupStatus::Found : JsonLookupStatus::Missing, std::move(result)};
}

}  // namespace

bool LocalTimestamp::valid() const noexcept {
    if (year < 2000 || year > 9999 || month < 1 || month > 12 || day < 1 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 ||
        second > 60 || millisecond < 0 || millisecond > 999) {
        return false;
    }
    static constexpr std::array<int, 12> daysPerMonth{
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    int maximum = daysPerMonth[static_cast<std::size_t>(month - 1)];
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    if (month == 2 && leap) maximum = 29;
    return day <= maximum;
}

std::optional<LocalTimestamp> parseSteamVrTimestamp(std::string_view line) noexcept {
    // Example: "Sun Aug 23 2026 19:27:59.354 [Info] - ..."
    if (line.size() < 24 || line[3] != ' ' || line[7] != ' ' || line[10] != ' ' ||
        line[15] != ' ' || line[18] != ':' || line[21] != ':') {
        return std::nullopt;
    }

    LocalTimestamp result;
    const auto month = std::find(kMonths.begin(), kMonths.end(), line.substr(4, 3));
    if (month == kMonths.end()) return std::nullopt;
    result.month = static_cast<int>(std::distance(kMonths.begin(), month)) + 1;
    if (!parseUnsigned(trim(line.substr(8, 2)), result.day) ||
        !parseUnsigned(line.substr(11, 4), result.year) ||
        !parseUnsigned(line.substr(16, 2), result.hour) ||
        !parseUnsigned(line.substr(19, 2), result.minute) ||
        !parseUnsigned(line.substr(22, 2), result.second)) {
        return std::nullopt;
    }

    if (line.size() >= 28 && line[24] == '.') {
        if (!parseUnsigned(line.substr(25, 3), result.millisecond)) return std::nullopt;
    }
    if (!result.valid()) return std::nullopt;
    return result;
}

SessionSummary parseVrServerLog(std::string_view utf8Log, const ParseOptions& options) {
    SessionSummary summary;
    bool pendingHookInstalled = false;

    std::size_t offset = 0;
    while (offset <= utf8Log.size()) {
        const std::size_t lineEnd = utf8Log.find('\n', offset);
        std::string_view line = utf8Log.substr(
            offset, lineEnd == std::string_view::npos ? utf8Log.size() - offset
                                                       : lineEnd - offset);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        offset = lineEnd == std::string_view::npos ? utf8Log.size() + 1 : lineEnd + 1;

        const auto timestamp = parseSteamVrTimestamp(line);
        if (!timestamp) continue;
        if (!summary.oldestTimestamp || *timestamp < *summary.oldestTimestamp) {
            summary.oldestTimestamp = timestamp;
        }
        if (!summary.newestTimestamp || *timestamp > *summary.newestTimestamp) {
            summary.newestTimestamp = timestamp;
        }
        if (options.sessionStart && *timestamp < *options.sessionStart) continue;
        summary.sawCurrentSessionLine = true;

        const std::size_t tagPosition = line.find(kPoseAnchorTag);
        if (tagPosition == std::string_view::npos) continue;
        const std::string_view message = line.substr(tagPosition + kPoseAnchorTag.size());

        if (message == "IVRServerDriverHost_006 pose hook installed" ||
            message == "IVRServerDriverHost_005 pose hook installed") {
            if (summary.loaded) summary.hookInstalled = true;
            else pendingHookInstalled = true;
            summary.hookFailure = false;
            continue;
        }

        constexpr std::string_view loadPrefix = "PoseAnchor ";
        constexpr std::string_view loadSuffix =
            " loaded (Vive Tracker only, normal poses pass through)";
        if (startsWith(message, loadPrefix) && message.size() > loadPrefix.size() + loadSuffix.size() &&
            message.substr(message.size() - loadSuffix.size()) == loadSuffix) {
            // A driver reload within one vrserver process starts a new status
            // epoch. Never attribute devices or interventions from an unloaded
            // copy to the copy that is active now.
            summary.trackers.clear();
            summary.holdTransitions = 0;
            summary.recoveryTransitions = 0;
            summary.lostTransitions = 0;
            summary.trackingTransitions = 0;
            summary.trackersTruncated = false;
            summary.recentEvents.clear();
            summary.version.assign(message.substr(
                loadPrefix.size(), message.size() - loadPrefix.size() - loadSuffix.size()));
            summary.sawLoadLine = true;
            summary.loaded = true;
            summary.hookInstalled = pendingHookInstalled;
            pendingHookInstalled = false;
            summary.hookFailure = false;
            summary.filterDisabled = false;
            continue;
        }

        if (message == "PoseAnchor unloaded") {
            summary.sawUnloadLine = true;
            summary.loaded = false;
            summary.hookInstalled = false;
            summary.hookFailure = false;
            summary.filterDisabled = false;
            pendingHookInstalled = false;
            continue;
        }
        if (message == "filterEnabled is false; no unsupported pose hook was installed") {
            summary.filterDisabled = true;
            summary.hookInstalled = false;
            pendingHookInstalled = false;
            continue;
        }
        if (message == "warning: no SteamVR pose hook was installed; filtering is inactive" ||
            (message.find("pose hook") != std::string_view::npos &&
             message.find("could not be hooked") != std::string_view::npos)) {
            summary.hookFailure = true;
            summary.hookInstalled = false;
            pendingHookInstalled = false;
            continue;
        }

        // Tracker messages cannot describe the active load epoch while the
        // driver is unloaded. Ignore any delayed or malformed log ordering.
        if (!summary.loaded) continue;

        if (const auto tracker = parseTracker(message)) {
            const auto existing = std::find_if(
                summary.trackers.begin(), summary.trackers.end(),
                [&tracker](const TrackerInfo& value) { return value.serial == tracker->serial; });
            if (existing != summary.trackers.end()) {
                *existing = *tracker;
            } else if (summary.trackers.size() < kMaximumTrackers) {
                summary.trackers.push_back(*tracker);
            } else {
                summary.trackersTruncated = true;
            }
            continue;
        }

        if (const auto event = parseTransition(message, *timestamp)) {
            const std::uint32_t count = event->suppressed == std::numeric_limits<std::uint32_t>::max()
                ? event->suppressed
                : event->suppressed + 1;
            if (event->toState == "hold") addSaturating(summary.holdTransitions, count);
            else if (event->toState == "recovering" ||
                     event->toState == "recovery") {
                addSaturating(summary.recoveryTransitions, count);
            }
            else if (event->toState == "lost") addSaturating(summary.lostTransitions, count);
            else if (event->toState == "tracking") addSaturating(summary.trackingTransitions, count);

            if (options.maxRecentEvents != 0) {
                if (summary.recentEvents.size() == options.maxRecentEvents) {
                    summary.recentEvents.erase(summary.recentEvents.begin());
                }
                summary.recentEvents.push_back(*event);
            }
        }
    }

    if (!summary.loaded) summary.hookInstalled = false;
    std::sort(summary.trackers.begin(), summary.trackers.end(),
              [](const TrackerInfo& left, const TrackerInfo& right) {
                  return left.serial < right.serial;
              });
    return summary;
}

JsonStringResult findJsonString(std::string_view json, std::string_view key) {
    const auto [status, value] = findTopLevelMember<std::string>(
        json, key, [](JsonCursor& cursor, std::string& output) {
            auto parsed = cursor.parseString();
            if (!parsed) return false;
            output = std::move(*parsed);
            return true;
        });
    return {status, value};
}

JsonStringArrayResult findJsonStringArray(std::string_view json, std::string_view key) {
    const auto [status, values] = findTopLevelMember<std::vector<std::string>>(
        json, key, [](JsonCursor& cursor, std::vector<std::string>& output) {
            if (!cursor.consume('[')) return false;
            cursor.skipWhitespace();
            if (cursor.consume(']')) return true;
            while (true) {
                auto value = cursor.parseString();
                if (!value) return false;
                output.push_back(std::move(*value));
                cursor.skipWhitespace();
                if (cursor.consume(']')) return true;
                if (!cursor.consume(',')) return false;
            }
        });
    return {status, values};
}

std::string normalizeWindowsPath(std::string_view path) {
    path = trim(path);
    if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
        path.remove_prefix(1);
        path.remove_suffix(1);
    }

    std::string output;
    output.reserve(path.size());
    bool lastWasSeparator = false;
    for (const unsigned char raw : path) {
        char value = static_cast<char>(raw);
        if (value == '/') value = '\\';
        const bool separator = value == '\\';
        // Preserve the leading UNC pair, collapse all other repeated separators.
        if (separator && lastWasSeparator && !(output.size() == 1 && output.front() == '\\')) {
            continue;
        }
        output.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(value))));
        lastWasSeparator = separator;
    }
    while (output.size() > 3 && output.back() == '\\') output.pop_back();
    return output;
}

ManifestPathKind classifyWindowsManifestPath(
    std::string_view path, bool mappedRemoteDrive) noexcept {
    path = trim(path);
    if (path.empty() || path.find('\0') != std::string_view::npos ||
        path.find('"') != std::string_view::npos) {
        return ManifestPathKind::Unsupported;
    }
    for (const unsigned char value : path) {
        if (value < 0x20) return ManifestPathKind::Unsupported;
    }
    if (mappedRemoteDrive) return ManifestPathKind::Remote;

    std::array<char, 16> prefix{};
    const std::size_t prefixLength = std::min(path.size(), prefix.size());
    for (std::size_t index = 0; index < prefixLength; ++index) {
        char value = path[index] == '/' ? '\\' : path[index];
        prefix[index] = static_cast<char>(
            std::tolower(static_cast<unsigned char>(value)));
    }
    const std::string_view start(prefix.data(), prefixLength);

    // Extended UNC paths must be recognized before the generic extended-local
    // prefix. Device namespaces are not normal driver roots and are never opened.
    if (startsWith(start, "\\\\?\\unc\\") || startsWith(start, "\\??\\unc\\")) {
        return ManifestPathKind::Remote;
    }
    if (startsWith(start, "\\\\.\\")) return ManifestPathKind::Unsupported;
    if (startsWith(start, "\\\\")) {
        if (startsWith(start, "\\\\?\\")) {
            // Permit only an extended drive path or a volume-GUID path. Other
            // extended namespaces can reach devices and are fail-closed.
            if (start.size() >= 7 &&
                (start[4] >= 'a' && start[4] <= 'z') &&
                start[5] == ':' && start[6] == '\\') {
                return ManifestPathKind::Local;
            }
            if (startsWith(start, "\\\\?\\volume")) return ManifestPathKind::Local;
            return ManifestPathKind::Unsupported;
        }
        return ManifestPathKind::Remote;
    }
    return ManifestPathKind::Local;
}

RegistrationState registrationStateFromInspection(
    std::size_t expectedMatches, std::size_t otherMatches,
    bool inspectionIncomplete) noexcept {
    if (inspectionIncomplete) return RegistrationState::InspectionIncomplete;
    if (expectedMatches == 1 && otherMatches == 0) {
        return RegistrationState::Registered;
    }
    if (expectedMatches > 1 || otherMatches > 1 ||
        (expectedMatches != 0 && otherMatches != 0)) {
        return RegistrationState::ConflictingRegistrations;
    }
    if (otherMatches != 0) return RegistrationState::RegisteredElsewhere;
    return RegistrationState::NotRegistered;
}

}  // namespace pose_anchor::status
