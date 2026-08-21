#pragma once

#include "device_registry.hpp"
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

    void filterPose(std::uint32_t deviceIndex, vr::DriverPose_t& pose);
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
        bool hasDriverPose{};
        vr::DriverPose_t lastDriverPose{};
        // Bumped whenever the armed state or stored pose changes, so RunFrame's
        // unlocked watchdog submit cannot clobber a slot re-armed by a fresh pose.
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
    bool filterEnabled_{true};
    bool syntheticWatchdogEnabled_{true};
    FilterConfig config_{};
    DeviceRegistry devices_{};
    std::array<DeviceSlot, vr::k_unMaxTrackedDeviceCount> slots_{};
    std::unique_ptr<InterfaceHooks> hooks_;
};

}  // namespace pose_anchor::driver
