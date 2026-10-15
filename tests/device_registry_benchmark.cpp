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
void consumePoseSample(const PoseSample& sample) noexcept;
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

struct AdapterResult {
    double nanosecondsPerSample{};
    std::uint64_t unexpectedChanges{};
};

// Exercise the production adapter calls made for every accepted tracker callback.
// Constant non-identity transforms match a calibrated tracking space and tracker
// attachment; the device position and timestamp continue to change each sample.
AdapterResult timeAdapter(vr::DriverPose_t pose, std::uint64_t iterations,
                         bool includeSampleConversion) {
    WorldFromDriver previousWorld{};
    DriverFromHead previousHead{};
    (void)PoseAdapter::readWorldFromDriver(pose, previousWorld);
    (void)PoseAdapter::readDriverFromHead(pose, previousHead);
    AdapterResult result{};
    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < iterations; ++i) {
        WorldFromDriver world{};
        DriverFromHead head{};
        if (!PoseAdapter::readWorldFromDriver(pose, world) ||
            !PoseAdapter::readDriverFromHead(pose, head)) {
            ++result.unexpectedChanges;
            continue;
        }
        result.unexpectedChanges += PoseAdapter::transformChanged(previousWorld, world);
        result.unexpectedChanges += PoseAdapter::transformChanged(previousHead, head);
        previousWorld = world;
        previousHead = head;
        if (includeSampleConversion) {
            pose.vecPosition[0] = static_cast<double>(i) * 0.001;
            pose_anchor::benchmark::consumePoseSample(
                PoseAdapter::toSample(pose, world, static_cast<std::int64_t>(i) * 1'000'000));
        }
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    result.nanosecondsPerSample = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()) /
        static_cast<double>(iterations);
    return result;
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

    const auto worldRotation = pose_anchor::fromRotationVector({0.1, 0.7, -0.2});
    const auto headRotation = pose_anchor::fromRotationVector({-0.3, 0.2, 0.4});
    pose.qWorldFromDriverRotation = {
        worldRotation.w, worldRotation.x, worldRotation.y, worldRotation.z};
    pose.qDriverFromHeadRotation = {
        headRotation.w, headRotation.x, headRotation.y, headRotation.z};
    pose.vecWorldFromDriverTranslation[0] = 1.0;
    pose.vecDriverFromHeadTranslation[1] = 0.03;
    pose.qRotation.w = 1.0;
    pose.vecVelocity[0] = 1.0;
    pose.result = vr::TrackingResult_Running_OK;
    const AdapterResult transforms = timeAdapter(pose, iterations, false);
    const AdapterResult adapter = timeAdapter(pose, iterations, true);

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
              << "static_transform_pair_ns=" << transforms.nanosecondsPerSample << '\n'
              << "adapter_pipeline_ns=" << adapter.nanosecondsPerSample << '\n'
              << "adapter_unexpected_changes="
              << transforms.unexpectedChanges + adapter.unexpectedChanges << '\n'
              << "checksum=" << pose_anchor::benchmark::driverPoseChecksum() << '\n';
    return transforms.unexpectedChanges == 0 && adapter.unexpectedChanges == 0 ? 0 : 1;
}
