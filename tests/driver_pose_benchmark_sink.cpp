#include <openvr_driver.h>

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

double driverPoseChecksum() noexcept { return consumedPoseChecksum; }

}  // namespace pose_anchor::benchmark
