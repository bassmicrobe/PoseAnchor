#include "pose_adapter.hpp"

#include <algorithm>
#include <cmath>

namespace pose_anchor::driver {
namespace {

Quat fromOpenVr(const vr::HmdQuaternion_t& q) { return {q.w, q.x, q.y, q.z}; }
vr::HmdQuaternion_t toOpenVr(Quat q) { return {q.w, q.x, q.y, q.z}; }
Vec3 fromArray(const double (&v)[3]) { return {v[0], v[1], v[2]}; }

void toArray(Vec3 v, double (&destination)[3]) {
    destination[0] = v.x;
    destination[1] = v.y;
    destination[2] = v.z;
}

bool usable(Quat q) {
    const double n2 = q.squaredNorm();
    return q.finite() && n2 >= 0.25 && n2 <= 2.25;
}

}  // namespace

bool PoseAdapter::readWorldFromDriver(const vr::DriverPose_t& pose,
                                      WorldFromDriver& transform) {
    const Quat rotation = fromOpenVr(pose.qWorldFromDriverRotation);
    const Vec3 translation = fromArray(pose.vecWorldFromDriverTranslation);
    if (!usable(rotation) || !translation.finite()) {
        return false;
    }
    transform.rotation = normalized(rotation);
    transform.translation = translation;
    return true;
}

bool PoseAdapter::transformChanged(const WorldFromDriver& a, const WorldFromDriver& b) {
    return (a.translation - b.translation).norm() > 0.001 ||
           angularDistance(a.rotation, b.rotation) > 0.0017453292519943296;  // 0.1 degrees
}

bool PoseAdapter::readDriverFromHead(const vr::DriverPose_t& pose,
                                     DriverFromHead& transform) {
    const Quat rotation = fromOpenVr(pose.qDriverFromHeadRotation);
    const Vec3 translation = fromArray(pose.vecDriverFromHeadTranslation);
    if (!usable(rotation) || !translation.finite()) {
        return false;
    }
    transform.rotation = normalized(rotation);
    transform.translation = translation;
    return true;
}

bool PoseAdapter::transformChanged(const DriverFromHead& a, const DriverFromHead& b) {
    return (a.translation - b.translation).norm() > 0.001 ||
           angularDistance(a.rotation, b.rotation) > 0.0017453292519943296;  // 0.1 degrees
}

PoseSample PoseAdapter::toSample(const vr::DriverPose_t& pose,
                                 const WorldFromDriver& transform,
                                 std::int64_t receiveTimeNs) {
    PoseSample sample{};
    const double offset = std::isfinite(pose.poseTimeOffset)
        ? std::clamp(pose.poseTimeOffset, -0.100, 0.050)
        : 0.0;
    sample.timeNs = receiveTimeNs + static_cast<std::int64_t>(offset * 1'000'000'000.0);
    sample.arrivalTimeNs = receiveTimeNs;

    if (!pose.deviceIsConnected) {
        sample.status = InputStatus::Disconnected;
    } else if (!pose.poseIsValid || pose.result != vr::TrackingResult_Running_OK) {
        sample.status = InputStatus::Invalid;
    } else {
        sample.status = InputStatus::RunningOk;
    }

    // Filter the driver's tracked reference point. SteamVR composes the preserved
    // driver-from-head transform after this pose when it exposes the device origin.
    const Vec3 driverPosition = fromArray(pose.vecPosition);
    const Quat driverRotation = fromOpenVr(pose.qRotation);
    sample.world.position = rotate(transform.rotation, driverPosition) + transform.translation;
    sample.world.orientation = usable(driverRotation)
        ? transform.rotation * driverRotation
        : driverRotation;
    sample.world.linearVelocity = rotate(transform.rotation, fromArray(pose.vecVelocity));
    sample.world.angularVelocity = rotate(transform.rotation, fromArray(pose.vecAngularVelocity));
    sample.world.linearAcceleration = rotate(transform.rotation, fromArray(pose.vecAcceleration));
    sample.world.angularAcceleration = rotate(transform.rotation, fromArray(pose.vecAngularAcceleration));
    return sample;
}

void PoseAdapter::applyOutput(vr::DriverPose_t& pose, const FilterOutput& output,
                              const WorldFromDriver& transform) {
    const Quat driverFromWorld = conjugate(transform.rotation);
    const Vec3 driverPosition = rotate(driverFromWorld, output.world.position - transform.translation);
    const Quat driverRotation = normalized(driverFromWorld * output.world.orientation);

    pose.qWorldFromDriverRotation = toOpenVr(transform.rotation);
    toArray(transform.translation, pose.vecWorldFromDriverTranslation);
    toArray(driverPosition, pose.vecPosition);
    pose.qRotation = toOpenVr(driverRotation);
    toArray(rotate(driverFromWorld, output.world.linearVelocity), pose.vecVelocity);
    toArray(rotate(driverFromWorld, output.world.angularVelocity), pose.vecAngularVelocity);
    toArray(rotate(driverFromWorld, output.world.linearAcceleration), pose.vecAcceleration);
    toArray(rotate(driverFromWorld, output.world.angularAcceleration), pose.vecAngularAcceleration);

    pose.poseIsValid = output.poseValid;
    pose.deviceIsConnected = output.connected;
    pose.result = output.poseValid
        ? vr::TrackingResult_Running_OK
        : (output.connected ? vr::TrackingResult_Running_OutOfRange
                            : vr::TrackingResult_Uninitialized);
    pose.poseTimeOffset = std::isfinite(pose.poseTimeOffset)
        ? std::clamp(pose.poseTimeOffset, -0.100, 0.050)
        : 0.0;
}

}  // namespace pose_anchor::driver
