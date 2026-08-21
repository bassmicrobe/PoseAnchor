#pragma once

#include "pose_anchor/tracker_filter.hpp"

#include <cstdint>
#include <openvr_driver.h>

namespace pose_anchor::driver {

struct WorldFromDriver {
    Quat rotation{};
    Vec3 translation{};
};

struct DriverFromHead {
    Quat rotation{};
    Vec3 translation{};
};

class PoseAdapter {
public:
    [[nodiscard]] static bool readWorldFromDriver(const vr::DriverPose_t& pose,
                                                  WorldFromDriver& transform);
    [[nodiscard]] static bool transformChanged(const WorldFromDriver& a,
                                               const WorldFromDriver& b);
    [[nodiscard]] static bool readDriverFromHead(const vr::DriverPose_t& pose,
                                                 DriverFromHead& transform);
    [[nodiscard]] static bool transformChanged(const DriverFromHead& a,
                                               const DriverFromHead& b);
    [[nodiscard]] static PoseSample toSample(const vr::DriverPose_t& pose,
                                             const WorldFromDriver& transform,
                                             std::int64_t receiveTimeNs);
    static void applyOutput(vr::DriverPose_t& pose, const FilterOutput& output,
                            const WorldFromDriver& transform);
};

}  // namespace pose_anchor::driver
