#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <openvr_driver.h>

namespace pose_anchor::driver {

enum class DeviceKind : std::uint8_t {
    Unknown,
    ViveTracker,
    Other,
};

struct DeviceMetadata {
    std::string serial;
    std::string model;
    std::string registeredType;
};

// Identity properties already fetched from SteamVR, separated from the fetch so
// the classification decision itself is unit-testable without a driver context.
struct DeviceIdentity {
    bool isGenericTracker{};
    std::string registeredType;
    std::string model;
    std::string trackingSystem;
    std::string actualTrackingSystem;
    std::string controllerType;
    std::string manufacturer;
};

// Pure decision: ViveTracker for a lighthouse-tracked device identifying as a
// Vive Tracker, Other for everything else. Callers own the fail-open retry
// handling for identities whose properties are still arriving.
[[nodiscard]] DeviceKind classifyIdentity(const DeviceIdentity& identity);

class DeviceRegistry {
public:
    DeviceRegistry();

    void reset();
    [[nodiscard]] DeviceKind kind(std::uint32_t index) const noexcept;
    void requestClassification(std::uint32_t index) noexcept;
    void classifyPending();
    [[nodiscard]] const DeviceMetadata& metadata(std::uint32_t index) const noexcept;

private:
    [[nodiscard]] DeviceKind classify(std::uint32_t index, DeviceMetadata& metadata,
                                      bool& retry) const;

    std::array<std::atomic<DeviceKind>, vr::k_unMaxTrackedDeviceCount> kinds_{};
    // SteamVR currently exposes 64 slots. A single mask lets RunFrame's steady-state
    // classification pass use one read instead of 64 atomic read-modify-writes.
    static_assert(vr::k_unMaxTrackedDeviceCount <= 64);
    static_assert(std::atomic_uint64_t::is_always_lock_free);
    std::atomic_uint64_t pendingMask_{};
    std::array<DeviceMetadata, vr::k_unMaxTrackedDeviceCount> metadata_{};
};

}  // namespace pose_anchor::driver
