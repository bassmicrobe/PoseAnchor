#pragma once

#include "device_registry.hpp"
#include "diagnostic_rate_limiter.hpp"
#include "pose_anchor/tracker_filter.hpp"
#include "pose_adapter.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <openvr_driver.h>

namespace pose_anchor::driver {

class InterfaceHooks;

class ServerProvider final : public vr::IServerTrackedDeviceProvider {
public:
    ServerProvider();
    ~ServerProvider();

    vr::EVRInitError Init(vr::IVRDriverContext* context) override;
    void Cleanup() override;
    const char* const* GetInterfaceVersions() override;
    void RunFrame() override;
    bool ShouldBlockStandbyMode() override;
    void EnterStandby() override;
    void LeaveStandby() override;

    // Cheap preflight keeps non-target callbacks off route bookkeeping and the
    // DriverPose_t copy/filter path. Unknown devices schedule RunFrame classification.
    [[nodiscard]] bool isFilterTarget(std::uint32_t deviceIndex) noexcept;
    // Called after isFilterTarget() succeeds. Returns false only if shutdown began
    // in between; on true, filteredPose is fully initialized.
    [[nodiscard]] bool filterPose(std::uint32_t deviceIndex,
                                  const vr::DriverPose_t& rawPose,
                                  vr::DriverPose_t& filteredPose);
    // Called with this device's forwarding lane held, after a watchdog snapshot
    // was built. A fresh callback/disconnect invalidates that snapshot before send.
    [[nodiscard]] bool isCurrentPoseGeneration(std::uint32_t deviceIndex,
                                               std::uint64_t generation) noexcept;
    void log(const std::string& message) const noexcept;

private:
    static constexpr std::size_t kFilterStateCount = 5;

    struct DeviceSlot {
        std::mutex mutex;
        std::unique_ptr<PoseFilter> filter;
        bool hasTransform{};
        WorldFromDriver transform{};
        bool hasDriverFromHead{};
        DriverFromHead driverFromHead{};
        FilterState lastState{FilterState::Cold};
        bool driverFromHeadWarningLogged{};
        bool identifiedLogged{};
        DiagnosticRateLimiter worldTransformLogRate;
        DiagnosticRateLimiter headTransformLogRate;
        bool hasDriverPose{};
        vr::DriverPose_t lastDriverPose{};
        // Bumped whenever the armed state or stored pose changes. Checked before
        // watchdog forwarding and before committing its terminal Lost one-shot.
        std::uint64_t generation{};
        std::array<std::int64_t, kFilterStateCount> lastTransitionLogNs{};
        std::array<std::uint32_t, kFilterStateCount> suppressedTransitionLogs{};
    };

    void loadSettings();
    void recordStateChange(std::uint32_t deviceIndex, DeviceSlot& slot,
                           const FilterOutput& output,
                           const FilterDecisionMetrics* metrics,
                           std::int64_t nowNs) const noexcept;
    void logStateChange(std::uint32_t deviceIndex, FilterState from,
                        const FilterOutput& output,
                        const FilterDecisionMetrics* metrics,
                        std::uint32_t suppressedLogs) const noexcept;
    static void resetTransitionLogRate(DeviceSlot& slot) noexcept;
    [[nodiscard]] static std::size_t stateIndex(FilterState state) noexcept;
    [[nodiscard]] static std::int64_t steadyNowNs();

    std::atomic_bool active_{false};
    std::atomic_uint32_t runFramesInFlight_{};
    bool filterEnabled_{true};
    bool syntheticWatchdogEnabled_{true};
    FilterConfig config_{};
    DeviceRegistry devices_{};
    std::array<DeviceSlot, vr::k_unMaxTrackedDeviceCount> slots_{};
    // Once allocated, retained until ServerProvider destruction. Cleanup removes
    // hooks without invalidating a pointer held by an overlapping RunFrame, and a
    // later Init safely reinstalls into the same InterfaceHooks object.
    std::unique_ptr<InterfaceHooks> hooks_;
};

}  // namespace pose_anchor::driver
