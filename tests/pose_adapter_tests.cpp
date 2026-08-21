#include "pose_adapter.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace pose_anchor;
using namespace pose_anchor::driver;

namespace {

int failures = 0;

void check(bool condition, std::string_view expression, int line) {
    if (!condition) {
        std::cerr << "line " << line << ": CHECK failed: " << expression << '\n';
        ++failures;
    }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

bool close(Vec3 a, Vec3 b, double tolerance = 1e-10) {
    return (a - b).norm() <= tolerance;
}

vr::HmdQuaternion_t openVr(Quat q) {
    return {q.w, q.x, q.y, q.z};
}

Vec3 array(const double (&value)[3]) {
    return {value[0], value[1], value[2]};
}

void set(Vec3 value, double (&destination)[3]) {
    destination[0] = value.x;
    destination[1] = value.y;
    destination[2] = value.z;
}

Quat quaternion(const vr::HmdQuaternion_t& value) {
    return {value.w, value.x, value.y, value.z};
}

void testNonIdentityDriverFromHeadRoundTrip() {
    const Quat worldRotation = fromRotationVector({0.1, 0.7, -0.2});
    const Vec3 worldTranslation{1.0, 2.0, 3.0};
    const Quat referenceRotation = fromRotationVector({-0.3, 0.2, 0.4});
    const Vec3 referencePosition{0.4, -0.2, 0.8};
    const Quat driverFromHeadRotation = fromRotationVector({0.5, -0.1, 0.25});
    const Vec3 driverFromHeadTranslation{0.04, 0.02, -0.07};

    vr::DriverPose_t pose{};
    pose.qWorldFromDriverRotation = openVr(worldRotation);
    set(worldTranslation, pose.vecWorldFromDriverTranslation);
    pose.qDriverFromHeadRotation = openVr(driverFromHeadRotation);
    set(driverFromHeadTranslation, pose.vecDriverFromHeadTranslation);
    pose.qRotation = openVr(referenceRotation);
    set(referencePosition, pose.vecPosition);
    set({1.2, -0.4, 0.7}, pose.vecVelocity);
    set({-0.3, 0.6, 0.8}, pose.vecAngularVelocity);
    set({2.0, 3.0, -1.0}, pose.vecAcceleration);
    set({0.2, -0.5, 0.1}, pose.vecAngularAcceleration);
    pose.poseIsValid = true;
    pose.deviceIsConnected = true;
    pose.result = vr::TrackingResult_Running_OK;

    WorldFromDriver worldFromDriver{};
    DriverFromHead driverFromHead{};
    CHECK(PoseAdapter::readWorldFromDriver(pose, worldFromDriver));
    CHECK(PoseAdapter::readDriverFromHead(pose, driverFromHead));
    CHECK(angularDistance(driverFromHead.rotation, driverFromHeadRotation) < 1e-7);
    CHECK(close(driverFromHead.translation, driverFromHeadTranslation));

    const PoseSample sample = PoseAdapter::toSample(pose, worldFromDriver, 1'000'000'000);
    CHECK(sample.arrivalTimeNs == 1'000'000'000);
    CHECK(close(sample.world.position,
                rotate(worldRotation, referencePosition) + worldTranslation));
    CHECK(angularDistance(sample.world.orientation,
                          worldRotation * referenceRotation) < 1e-7);

    const Quat originalDriverFromHeadRotation = quaternion(pose.qDriverFromHeadRotation);
    const Vec3 originalDriverFromHeadTranslation = array(pose.vecDriverFromHeadTranslation);
    FilterOutput output{};
    output.world = sample.world;
    output.poseValid = true;
    output.connected = true;
    PoseAdapter::applyOutput(pose, output, worldFromDriver);

    CHECK(close(array(pose.vecPosition), referencePosition));
    CHECK(angularDistance(quaternion(pose.qRotation), referenceRotation) < 1e-7);
    CHECK(angularDistance(quaternion(pose.qDriverFromHeadRotation),
                          originalDriverFromHeadRotation) < 1e-7);
    CHECK(close(array(pose.vecDriverFromHeadTranslation),
                originalDriverFromHeadTranslation));

    const Quat finalWorldRotation =
        worldRotation * referenceRotation * driverFromHeadRotation;
    const Vec3 finalWorldPosition = worldTranslation + rotate(
        worldRotation,
        referencePosition + rotate(referenceRotation, driverFromHeadTranslation));
    const Quat roundTripFinalRotation = worldRotation * quaternion(pose.qRotation) *
                                       quaternion(pose.qDriverFromHeadRotation);
    const Vec3 roundTripFinalPosition = worldTranslation + rotate(
        worldRotation,
        array(pose.vecPosition) +
            rotate(quaternion(pose.qRotation), array(pose.vecDriverFromHeadTranslation)));
    CHECK(angularDistance(roundTripFinalRotation, finalWorldRotation) < 1e-7);
    CHECK(close(roundTripFinalPosition, finalWorldPosition));
}

void testModifiedPoseRoundTripAndValidation() {
    vr::DriverPose_t pose{};
    pose.qWorldFromDriverRotation = openVr(fromRotationVector({0.2, -0.4, 0.1}));
    set({-1.0, 0.5, 2.0}, pose.vecWorldFromDriverTranslation);
    pose.qDriverFromHeadRotation = openVr(fromRotationVector({0.4, 0.1, -0.3}));
    set({0.03, -0.05, 0.02}, pose.vecDriverFromHeadTranslation);
    pose.qRotation.w = 1.0;
    pose.poseIsValid = true;
    pose.deviceIsConnected = true;
    pose.result = vr::TrackingResult_Running_OK;

    WorldFromDriver transform{};
    DriverFromHead driverFromHead{};
    CHECK(PoseAdapter::readWorldFromDriver(pose, transform));
    CHECK(PoseAdapter::readDriverFromHead(pose, driverFromHead));

    FilterOutput output{};
    output.world.position = {2.0, 1.0, -0.5};
    output.world.orientation = fromRotationVector({-0.2, 0.6, 0.3});
    output.world.linearVelocity = {1.0, 2.0, 3.0};
    output.world.angularVelocity = {-0.4, 0.7, 0.2};
    output.world.linearAcceleration = {0.5, -0.2, 0.9};
    output.world.angularAcceleration = {-0.1, 0.3, 0.8};
    output.poseValid = true;
    output.connected = true;
    PoseAdapter::applyOutput(pose, output, transform);
    const PoseSample roundTrip = PoseAdapter::toSample(pose, transform, 0);
    CHECK(close(roundTrip.world.position, output.world.position));
    CHECK(angularDistance(roundTrip.world.orientation, output.world.orientation) < 1e-7);
    CHECK(close(roundTrip.world.linearVelocity, output.world.linearVelocity));
    CHECK(close(roundTrip.world.angularVelocity, output.world.angularVelocity));
    CHECK(close(roundTrip.world.linearAcceleration, output.world.linearAcceleration));
    CHECK(close(roundTrip.world.angularAcceleration, output.world.angularAcceleration));

    DriverFromHead changed = driverFromHead;
    changed.translation.x += 0.01;
    CHECK(PoseAdapter::transformChanged(driverFromHead, changed));
    CHECK(!PoseAdapter::transformChanged(driverFromHead, driverFromHead));
    pose.qDriverFromHeadRotation = {};
    CHECK(!PoseAdapter::readDriverFromHead(pose, changed));
}

}  // namespace

int main() {
    testNonIdentityDriverFromHeadRoundTrip();
    testModifiedPoseRoundTripAndValidation();
    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All PoseAnchor pose adapter tests passed\n";
    return EXIT_SUCCESS;
}
