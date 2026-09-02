#include "device_registry.hpp"
#include "hooks.hpp"
#include "server_provider.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>

using namespace pose_anchor::driver;

namespace pose_anchor::benchmark {
void consumeDriverPose(const vr::DriverPose_t& pose) noexcept;
double driverPoseChecksum() noexcept;
}  // namespace pose_anchor::benchmark

namespace {

template <typename Forward>
double timePoseForwarding(Forward&& forward, vr::DriverPose_t& pose,
                          std::uint64_t iterations) {
    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < iterations; ++i) {
        pose.poseTimeOffset = static_cast<double>(i) * 1e-9;
        forward(pose);
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()) /
        static_cast<double>(iterations);
}

}  // namespace

int main() {
    constexpr std::uint64_t iterations = 10'000'000;
    constexpr std::uint64_t watchdogIterations = 1'000'000;
    DeviceRegistry registry;

    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < iterations; ++i) {
        registry.classifyPending();
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const double totalNanoseconds = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());

    std::uint64_t trackerVisits = 0;
    const auto watchdogStarted = std::chrono::steady_clock::now();
    for (std::uint64_t iteration = 0; iteration < watchdogIterations; ++iteration) {
        for (std::uint32_t index = 0; index < vr::k_unMaxTrackedDeviceCount; ++index) {
            trackerVisits += registry.kind(index) == DeviceKind::ViveTracker ? 1u : 0u;
        }
    }
    const auto watchdogElapsed = std::chrono::steady_clock::now() - watchdogStarted;
    const double watchdogNanoseconds = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(watchdogElapsed).count());

    std::atomic_uint32_t callbackRundown{};
    const auto rundownStarted = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < iterations; ++i) {
        callbackRundown.fetch_add(1, std::memory_order_acq_rel);
        callbackRundown.fetch_sub(1, std::memory_order_acq_rel);
    }
    const auto rundownElapsed = std::chrono::steady_clock::now() - rundownStarted;

    std::mutex slotMutex;
    const auto mutexStarted = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < iterations; ++i) {
        slotMutex.lock();
        slotMutex.unlock();
    }
    const auto mutexElapsed = std::chrono::steady_clock::now() - mutexStarted;

    std::int64_t clockChecksum = 0;
    const auto clockStarted = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < iterations; ++i) {
        clockChecksum ^= std::chrono::steady_clock::now().time_since_epoch().count();
    }
    const auto clockElapsed = std::chrono::steady_clock::now() - clockStarted;

    const auto nanosecondsPerIteration = [iterations](auto duration) {
        return static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count()) /
            static_cast<double>(iterations);
    };

    vr::DriverPose_t pose{};
    pose.deviceIsConnected = true;
    pose.poseIsValid = true;
    const double rawForwardNanoseconds = timePoseForwarding(
        [](const vr::DriverPose_t& value) {
            pose_anchor::benchmark::consumeDriverPose(value);
        }, pose, iterations);
    const double copiedForwardNanoseconds = timePoseForwarding(
        [](const vr::DriverPose_t& value) {
            const vr::DriverPose_t copy = value;
            pose_anchor::benchmark::consumeDriverPose(copy);
        }, pose, iterations);

    std::cout << std::fixed << std::setprecision(2)
              << "iterations=" << iterations << '\n'
              << "idle_scan_ns=" << totalNanoseconds / static_cast<double>(iterations)
              << '\n'
              << "watchdog_64_slot_scan_ns="
              << watchdogNanoseconds / static_cast<double>(watchdogIterations) << '\n'
              << "watchdog_tracker_visits=" << trackerVisits << '\n'
              << "callback_rundown_pair_ns="
              << nanosecondsPerIteration(rundownElapsed) << '\n'
              << "uncontended_mutex_pair_ns="
              << nanosecondsPerIteration(mutexElapsed) << '\n'
              << "steady_clock_now_ns=" << nanosecondsPerIteration(clockElapsed) << '\n'
              << "atomic_checksums=" << callbackRundown.load(std::memory_order_relaxed)
              << ',' << clockChecksum << '\n'
              << "device_registry_bytes=" << sizeof(DeviceRegistry) << '\n'
              << "server_provider_bytes=" << sizeof(ServerProvider) << '\n'
              << "interface_hooks_bytes=" << sizeof(InterfaceHooks) << '\n'
              << "driver_pose_bytes=" << sizeof(vr::DriverPose_t) << '\n'
              << "raw_forward_ns=" << rawForwardNanoseconds << '\n'
              << "copied_forward_ns=" << copiedForwardNanoseconds << '\n'
              << "copy_overhead_ns=" << copiedForwardNanoseconds - rawForwardNanoseconds
              << '\n'
              << "checksum=" << pose_anchor::benchmark::driverPoseChecksum() << '\n';
    return 0;
}
