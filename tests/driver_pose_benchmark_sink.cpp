#include <openvr_driver.h>
#include "pose_anchor/tracker_filter.hpp"

namespace pose_anchor::benchmark {
namespace {

volatile double consumedPoseChecksum{};

}  // namespace

// This separate translation unit makes the pose opaque to the forwarding benchmark.
// It models SteamVR's external callback and prevents dead-store elimination from
// turning a DriverPose_t copy into a copy of only the one field consumed here.
void consumeDriverPose(const vr::DriverPose_t& pose) noexcept {
    const double previous = consumedPoseChecksum;
    consumedPoseChecksum = previous + pose.poseTimeOffset;
}

void consumePoseSample(const PoseSample& sample) noexcept {
    const double previous = consumedPoseChecksum;
    consumedPoseChecksum = previous + sample.world.position.x;
}

double driverPoseChecksum() noexcept { return consumedPoseChecksum; }

}  // namespace pose_anchor::benchmark
