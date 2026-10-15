#include "status_parser.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <new>
#include <string>
#include <string_view>

namespace {
std::atomic_size_t allocationCount{};
}

void* operator new(std::size_t size) {
    if (void* memory = std::malloc(size == 0 ? 1 : size)) {
        allocationCount.fetch_add(1, std::memory_order_relaxed);
        return memory;
    }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
constexpr std::size_t kLogBytes = 16 * 1024 * 1024;
constexpr std::string_view kPrefix =
    "Mon Oct 05 2026 20:30:00.001 [Info] - pose_anchor: [PoseAnchor] ";

std::string makeLog(bool events) {
    std::string result;
    result.reserve(kLogBytes);
    result += kPrefix;
    result += "IVRServerDriverHost_006 pose hook installed\n";
    result += kPrefix;
    result += "PoseAnchor 0.1.0 loaded (Vive Tracker only, normal poses pass through)\n";
    result += kPrefix;
    result += "identified Vive Tracker LHR-123456789ABCDEF (VIVE Tracker 3.0) "
              "driverFromHead q=(1,0,0,0) t=(0,0,0)\n";
    const std::string line = events
        ? std::string(kPrefix) + "tracker LHR-123456789ABCDEF tracking -> hold "
          "reasons=0x81 dt_ms=1.0000\n"
        : "Mon Oct 05 2026 20:30:00.002 [Info] - lighthouse: received ordinary "
          "device diagnostics; no PoseAnchor event is present in this line\n";
    while (result.size() + line.size() <= kLogBytes) result += line;
    return result;
}

void measure(std::string_view label, const std::string& log, std::size_t recentLimit) {
    pose_anchor::status::ParseOptions options;
    options.sessionStart = pose_anchor::status::LocalTimestamp{2026, 10, 5, 20, 29, 59, 0};
    options.maxRecentEvents = recentLimit;
    std::array<double, 11> samples{};
    std::size_t measuredAllocations{};
    std::size_t checksum{};
    for (std::size_t run = 0; run < samples.size() + 2; ++run) {
        const auto before = allocationCount.load(std::memory_order_relaxed);
        const auto start = std::chrono::steady_clock::now();
        const auto result = pose_anchor::status::parseVrServerLog(log, options);
        const double milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        const auto allocations = allocationCount.load(std::memory_order_relaxed) - before;
        checksum += result.holdTransitions + result.recentEvents.size() + result.trackers.size();
        if (!result.loaded || !result.hookInstalled) std::exit(EXIT_FAILURE);
        if (run >= 2) {
            samples[run - 2] = milliseconds;
            measuredAllocations = allocations;
        }
    }
    std::sort(samples.begin(), samples.end());
    std::cout << label << ": bytes=" << log.size()
              << " recent_limit=" << recentLimit
              << " median_ms=" << samples[samples.size() / 2]
              << " allocations_per_parse=" << measuredAllocations
              << " checksum=" << checksum << '\n';
}
}  // namespace

int main() {
    std::cout << std::fixed << std::setprecision(3);
    const auto ordinary = makeLog(false);
    const auto events = makeLog(true);
    measure("ordinary", ordinary, 5);
    measure("event_heavy", events, 5);
    measure("event_history", events, 256);
    return EXIT_SUCCESS;
}
