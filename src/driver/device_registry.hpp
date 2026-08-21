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
    std::array<std::atomic_bool, vr::k_unMaxTrackedDeviceCount> pending_{};
    std::array<DeviceMetadata, vr::k_unMaxTrackedDeviceCount> metadata_{};
};

}  // namespace pose_anchor::driver
