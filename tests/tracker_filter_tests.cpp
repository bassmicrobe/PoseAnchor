#include "pose_anchor/tracker_filter.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>

using namespace pose_anchor;

namespace {

static_assert(std::is_trivially_copyable_v<FilterDecisionMetrics>);
static_assert(std::is_standard_layout_v<FilterDecisionMetrics>);
static_assert(sizeof(FilterDecisionMetrics) <= 256);

int failures = 0;

void check(bool condition, std::string_view expression, int line) {
    if (!condition) {
        std::cerr << "line " << line << ": CHECK failed: " << expression << '\n';
        ++failures;
    }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

constexpr std::int64_t ms(double value) {
    return static_cast<std::int64_t>(value * 1'000'000.0);
}

PoseSample sample(std::int64_t timeNs, Vec3 position = {}, Quat orientation = {},
                  Vec3 velocity = {}, Vec3 angularVelocity = {},
                  InputStatus status = InputStatus::RunningOk) {
    PoseSample result{};
    result.timeNs = timeNs;
    result.arrivalTimeNs = timeNs;
    result.world.position = position;
    result.world.orientation = orientation;
    result.world.linearVelocity = velocity;
    result.world.angularVelocity = angularVelocity;
    result.status = status;
    return result;
}

std::int64_t warmStationary(PoseFilter& filter, int count = 8) {
    std::int64_t time = 0;
    for (int i = 0; i < count; ++i) {
        time += ms(10.0);
        const auto output = filter.push(sample(time));
        if (i + 1 < count) {
            CHECK(!output.poseValid);
            CHECK(output.modified);
        } else {
            CHECK(output.poseValid);
            CHECK(!output.modified);
        }
    }
    CHECK(filter.state() == FilterState::Tracking);
    CHECK(!filter.lastDecisionMetrics().valid);
    return time;
}

void testMath() {
    const Vec3 rotation{0.3, -0.4, 0.2};
    const Vec3 roundTrip = rotationVector(fromRotationVector(rotation));
    CHECK((roundTrip - rotation).norm() < 1e-10);

    const Quat q = fromRotationVector({0.0, 3.141592653589793, 0.0});
    CHECK(angularDistance(q, -q) < 1e-12);

    const Vec3 derivative{1.2, -0.8, 0.4};
    const Vec3 mapped = leftJacobianApply(rotation, derivative);
    const Vec3 inverse = leftJacobianInverseApply(rotation, mapped);
    CHECK((inverse - derivative).norm() < 1e-10);

    const Quat worldFromDriver = fromRotationVector({0.0, 1.5707963267948966, 0.0});
    const Vec3 translation{1.0, 2.0, 3.0};
    const Vec3 local{0.4, -0.2, 0.8};
    const Vec3 world = rotate(worldFromDriver, local) + translation;
    const Vec3 recovered = rotate(conjugate(worldFromDriver), world - translation);
    CHECK((recovered - local).norm() < 1e-12);
}

void testOneFrameSpike() {
    PoseFilter filter;
    std::int64_t time = warmStationary(filter);

    time += ms(10.0);
    const auto spike = filter.push(sample(
        time, {1.5, 0.0, 0.0}, fromRotationVector({0.0, 2.0943951023931953, 0.0})));
    CHECK(spike.state == FilterState::Hold);
    CHECK(spike.poseValid);
    CHECK(spike.modified);
    CHECK(spike.world.position.norm() < 1e-9);

    time += ms(10.0);
    const auto recovered = filter.push(sample(time));
    CHECK(recovered.state == FilterState::Tracking);
    CHECK(!recovered.modified);
    CHECK(recovered.world.position.norm() < 1e-9);
}

void testCandidateVelocityCannotMaskSpike() {
    PoseFilter filter;
    std::int64_t time = warmStationary(filter);

    time += ms(10.0);
    const auto positionSpike = filter.push(sample(
        time, {0.10, 0.0, 0.0}, {}, {15.0, 0.0, 0.0}));
    CHECK(positionSpike.state == FilterState::Hold);
    CHECK(positionSpike.modified);
    CHECK(positionSpike.world.position.norm() < 1e-9);

    PoseFilter angularFilter;
    time = warmStationary(angularFilter);
    time += ms(10.0);
    const auto rotationSpike = angularFilter.push(sample(
        time, {}, fromRotationVector({0.0, 0.25, 0.0}), {}, {0.0, 35.0, 0.0}));
    CHECK(rotationSpike.state == FilterState::Hold);
    CHECK(rotationSpike.modified);
    CHECK((rotationSpike.reasons & ReasonRotationInnovation) != 0);
    CHECK((rotationSpike.reasons & ReasonAngularSpeed) == 0);
    CHECK(angularDistance(rotationSpike.world.orientation, {}) < 1e-9);
    const auto rotationMetrics = angularFilter.lastDecisionMetrics();
    CHECK(rotationMetrics.valid);
    CHECK((rotationMetrics.exceededGates & DecisionGateAngularVelocityDelta) != 0);
    CHECK((rotationMetrics.rejectedGates & DecisionGateRotationInnovation) != 0);
    CHECK((rotationMetrics.rejectedGates & DecisionGateAngularVelocityDelta) == 0);

    PoseFilter velocityOnlyFilter;
    time = warmStationary(velocityOnlyFilter);
    time += ms(10.0);
    const auto velocityOnlySpike = velocityOnlyFilter.push(sample(
        time, {}, {}, {14.9, 0.0, 0.0}));
    CHECK(velocityOnlySpike.state == FilterState::Tracking);
    CHECK(velocityOnlySpike.poseValid);
    CHECK(velocityOnlySpike.modified);
    CHECK((velocityOnlySpike.reasons & ReasonVelocityRepaired) != 0);
    CHECK(velocityOnlySpike.world.position.norm() < 1e-9);
    CHECK(velocityOnlySpike.world.linearVelocity.norm() < 1e-9);
    const auto velocityMetrics = velocityOnlyFilter.lastDecisionMetrics();
    CHECK(velocityMetrics.valid);
    CHECK(velocityMetrics.linearVelocityRepaired);
    CHECK((velocityMetrics.exceededGates & DecisionGateLinearVelocityDelta) != 0);
    CHECK((velocityMetrics.exceededGates & DecisionGateLinearPoseConsistency) != 0);
    CHECK(velocityMetrics.rejectedGates == DecisionGateNone);
}

void testHighAngularAccelerationIsPoseCorroborated() {
    PoseFilter filter;
    std::int64_t time = warmStationary(filter);
    constexpr double dt = 0.010;
    constexpr double angularAcceleration = 2000.0;
    double angle = 0.0;
    double angularVelocity = 0.0;

    for (int i = 0; i < 4; ++i) {
        angle += angularVelocity * dt + 0.5 * angularAcceleration * dt * dt;
        angularVelocity += angularAcceleration * dt;
        time += ms(10.0);
        const auto output = filter.push(sample(
            time, {}, fromRotationVector({0.0, angle, 0.0}), {},
            {0.0, angularVelocity, 0.0}));
        CHECK(output.state == FilterState::Tracking);
        CHECK(output.poseValid);
        CHECK(output.modified);
        CHECK((output.reasons & ReasonVelocityRepaired) != 0);
        CHECK((output.reasons & ReasonRotationInnovation) == 0);
        CHECK((output.reasons & ReasonAngularSpeed) == 0);

        const auto metrics = filter.lastDecisionMetrics();
        CHECK(metrics.valid);
        CHECK(metrics.angularVelocityRepaired);
        CHECK((metrics.exceededGates & DecisionGateAngularVelocityDelta) != 0);
        CHECK((metrics.rejectedGates & DecisionGateRotationInnovation) == 0);
        CHECK(metrics.repairedAngularSpeedRadiansPerSecond <=
              filter.config().hardMaxAngularSpeed + 1e-9);
    }
    CHECK(filter.state() == FilterState::Tracking);
    CHECK(filter.diagnostics().rejectedSamples == 0);
}

void testAngularVelocityPoseMismatchIsRepaired() {
    PoseFilter filter;
    std::int64_t time = warmStationary(filter);
    time += ms(10.0);
    const auto output = filter.push(sample(
        time, {}, fromRotationVector({0.0, 0.10, 0.0}), {}, {0.0, 40.0, 0.0}));

    CHECK(output.state == FilterState::Tracking);
    CHECK(output.poseValid);
    CHECK(output.modified);
    CHECK((output.reasons & ReasonVelocityRepaired) != 0);
    CHECK(std::abs(output.world.angularVelocity.y - 10.0) < 1e-9);
    const auto metrics = filter.lastDecisionMetrics();
    CHECK(metrics.valid);
    CHECK(metrics.angularVelocityRepaired);
    CHECK((metrics.exceededGates & DecisionGateAngularPoseConsistency) != 0);
    CHECK(metrics.angularPoseResidualRadians > metrics.angularPoseResidualGateRadians);
    CHECK(std::abs(metrics.angularSpeedRadiansPerSecond - 40.0) < 1e-9);
    CHECK(std::abs(metrics.repairedAngularSpeedRadiansPerSecond - 10.0) < 1e-9);
    CHECK(metrics.rejectedGates == DecisionGateNone);
}

void testShortPeriodAngularVelocityPoisonIsRepaired() {
    PoseFilter filter;
    std::int64_t time = warmStationary(filter);

    for (int i = 0; i < 8; ++i) {
        time += ms(1.0);
        const auto output = filter.push(sample(
            time, {}, {}, {}, {0.0, 20.0, 0.0}));
        CHECK(output.state == FilterState::Tracking);
        CHECK(output.poseValid);
        CHECK(output.modified);
        CHECK((output.reasons & ReasonVelocityRepaired) != 0);
        CHECK(output.world.angularVelocity.norm() < 1e-9);

        const auto metrics = filter.lastDecisionMetrics();
        CHECK(metrics.valid);
        CHECK(std::abs(metrics.rawDtSeconds - 0.001) < 1e-12);
        CHECK(metrics.angularVelocityRepaired);
        CHECK((metrics.exceededGates & DecisionGateAngularVelocityDelta) != 0);
        CHECK(metrics.rejectedGates == DecisionGateNone);
    }
    CHECK(filter.diagnostics().rejectedSamples == 0);
}

void testAngularRepairDoesNotOscillateAfterStop() {
    PoseFilter filter;
    std::int64_t time = 0;
    double angle = 0.0;
    for (int i = 0; i < 8; ++i) {
        time += ms(10.0);
        angle += 0.20;
        const auto output = filter.push(sample(
            time, {}, fromRotationVector({0.0, angle, 0.0}), {}, {0.0, 20.0, 0.0}));
        if (i == 7) CHECK(output.state == FilterState::Tracking);
    }

    for (int i = 0; i < 6; ++i) {
        time += ms(10.0);
        const auto output = filter.push(sample(
            time, {}, fromRotationVector({0.0, angle, 0.0}), {}, {0.0, 100.0, 0.0}));
        CHECK(output.state == FilterState::Tracking);
        CHECK(output.poseValid);
        CHECK(output.modified);
        CHECK((output.reasons & ReasonVelocityRepaired) != 0);
        CHECK(output.world.angularVelocity.norm() < 1e-9);
        const auto metrics = filter.lastDecisionMetrics();
        CHECK(metrics.valid);
        CHECK(metrics.angularVelocityRepaired);
        CHECK(metrics.repairedAngularSpeedRadiansPerSecond < 1e-9);
        CHECK(metrics.rejectedGates == DecisionGateNone);
    }
    CHECK(filter.diagnostics().rejectedSamples == 0);
}

void testHighRateManualRotationTraceAndPoseJump() {
    PoseFilter filter;
    std::int64_t time = warmStationary(filter);
    double angle = 0.0;
    double previousActualAngularVelocity = 0.0;
    bool sawHardSpeedRepair = false;

    for (int i = 0; i < 500; ++i) {
        const double dt = (i & 1) == 0 ? 0.001 : 0.002;
        const double actualAngularVelocity = 28.0 + 12.0 * std::sin(0.071 * i);
        angle += 0.5 * (previousActualAngularVelocity + actualAngularVelocity) * dt;
        previousActualAngularVelocity = actualAngularVelocity;
        const double reportedNoise = (i % 4 == 0) ? 36.0
                                   : (i % 4 == 1) ? -18.0
                                   : (i % 4 == 2) ? 22.0
                                                  : -12.0;
        const double reportedAngularVelocity = actualAngularVelocity + reportedNoise;
        time += static_cast<std::int64_t>(dt * 1'000'000'000.0);

        const auto output = filter.push(sample(
            time, {}, fromRotationVector({0.0, angle, 0.0}), {},
            {0.0, reportedAngularVelocity, 0.0}));
        CHECK(output.state == FilterState::Tracking);
        CHECK(output.poseValid);
        CHECK((output.reasons & ReasonRotationInnovation) == 0);
        CHECK(output.world.angularVelocity.norm() <=
              filter.config().hardMaxAngularSpeed + 1e-9);

        const auto metrics = filter.lastDecisionMetrics();
        CHECK(metrics.valid);
        if ((metrics.exceededGates & DecisionGateAngularHardSpeed) != 0) {
            sawHardSpeedRepair = true;
            CHECK(metrics.angularVelocityRepaired);
        }
    }
    CHECK(sawHardSpeedRepair);
    CHECK(filter.state() == FilterState::Tracking);
    CHECK(filter.diagnostics().rejectedSamples == 0);
    CHECK(filter.diagnostics().velocityRepairs > 0);

    time += ms(1.0);
    angle += 0.60;
    const auto poseJump = filter.push(sample(
        time, {}, fromRotationVector({0.0, angle, 0.0}), {},
        {0.0, previousActualAngularVelocity, 0.0}));
    CHECK(poseJump.state == FilterState::Hold);
    CHECK((poseJump.reasons & ReasonRotationInnovation) != 0);
    CHECK((poseJump.reasons & ReasonAngularSpeed) == 0);
    const auto jumpMetrics = filter.lastDecisionMetrics();
    CHECK(jumpMetrics.valid);
    CHECK((jumpMetrics.rejectedGates & DecisionGateRotationInnovation) != 0);
}

void testDropoutAndLoss() {
    PoseFilter filter;
    std::int64_t time = warmStationary(filter);
    time += ms(10.0);
    auto output = filter.push(sample(time, {}, {}, {}, {}, InputStatus::Invalid));
    CHECK(output.state == FilterState::Hold);
    CHECK(output.poseValid);
    CHECK(output.synthetic);

    time += ms(50.0);
    output = filter.push(sample(time, {}, {}, {}, {}, InputStatus::Invalid));
    CHECK(output.state == FilterState::Hold);
    CHECK(output.poseValid);

    time += ms(10.0);
    output = filter.push(sample(time));
    CHECK(output.state == FilterState::Tracking);
    CHECK(output.poseValid);

    time += ms(10.0);
    (void)filter.push(sample(time, {}, {}, {}, {}, InputStatus::Invalid));
    time += ms(170.0);
    output = filter.push(sample(time, {}, {}, {}, {}, InputStatus::Invalid));
    CHECK(output.state == FilterState::Lost);
    CHECK(!output.poseValid);
    CHECK(output.modified);
}

void testPersistentRelocalization() {
    PoseFilter filter;
    std::int64_t time = warmStationary(filter);
    const Vec3 relocated{0.4, 0.0, 0.0};

    time += ms(10.0);
    auto output = filter.push(sample(time, relocated));
    CHECK(output.state == FilterState::Hold);
    CHECK(output.world.position.norm() < 1e-9);

    for (int i = 0; i < 3; ++i) {
        time += ms(10.0);
        output = filter.push(sample(time, relocated));
    }
    CHECK(output.state == FilterState::Recovering);
    CHECK(output.modified);
    CHECK(output.world.position.norm() < 1e-8);

    Vec3 previous = output.world.position;
    for (int i = 0; i < 45; ++i) {
        time += ms(10.0);
        output = filter.push(sample(time, relocated));
        CHECK((output.world.position - previous).norm() < 0.08);
        previous = output.world.position;
    }
    CHECK(output.state == FilterState::Tracking);
    CHECK((output.world.position - relocated).norm() < 1e-12);
}

void testLegitimateMotionAndQuaternionSigns() {
    PoseFilter filter;
    constexpr double dt = 0.010;
    std::int64_t time = 0;
    for (int i = 1; i <= 200; ++i) {
        time += ms(10.0);
        const double seconds = i * dt;
        Quat orientation = fromRotationVector({0.0, 8.0 * seconds, 0.0});
        if ((i & 1) != 0) orientation = -orientation;
        const auto output = filter.push(sample(
            time, {3.0 * seconds, 0.0, 0.0}, orientation,
            {3.0, 0.0, 0.0}, {0.0, 8.0, 0.0}));
        CHECK(output.state != FilterState::Hold);
        CHECK(output.state != FilterState::Lost);
        if (i < 8) CHECK(!output.poseValid);
        else CHECK(output.poseValid);
    }
    CHECK(filter.diagnostics().rejectedSamples == 0);
}

void testLegitimateAcceleration() {
    PoseFilter filter;
    std::int64_t time = warmStationary(filter);
    constexpr double dt = 0.010;
    constexpr double linearAcceleration = 60.0;
    constexpr double angularAcceleration = 300.0;
    double position = 0.0;
    double angle = 0.0;
    double velocity = 0.0;
    double angularVelocity = 0.0;

    for (int i = 0; i < 10; ++i) {
        position += velocity * dt + 0.5 * linearAcceleration * dt * dt;
        angle += angularVelocity * dt + 0.5 * angularAcceleration * dt * dt;
        velocity += linearAcceleration * dt;
        angularVelocity += angularAcceleration * dt;
        time += ms(10.0);
        const auto output = filter.push(sample(
            time, {position, 0.0, 0.0}, fromRotationVector({0.0, angle, 0.0}),
            {velocity, 0.0, 0.0}, {0.0, angularVelocity, 0.0}));
        CHECK(output.state == FilterState::Tracking);
        CHECK(output.poseValid);
    }
}

void testColdRequiresCoherentSamples() {
    PoseFilter filter;
    std::int64_t time = 0;
    for (int i = 0; i < 24; ++i) {
        time += ms(10.0);
        const Vec3 position = (i & 1) == 0 ? Vec3{} : Vec3{2.0, 0.0, 0.0};
        const auto output = filter.push(sample(time, position));
        CHECK(!output.poseValid);
        CHECK(output.modified);
        CHECK(filter.state() == FilterState::Cold);
    }
}

void testUnsafeDirectReturnIsRejected() {
    PoseFilter filter;
    std::int64_t time = warmStationary(filter);
    time += ms(10.0);
    (void)filter.push(sample(time, {}, {}, {}, {}, InputStatus::Invalid));
    time += ms(10.0);
    const auto output = filter.push(sample(time, {}, {}, {100.0, 0.0, 0.0}));
    CHECK(output.state == FilterState::Hold);
    CHECK(output.modified);
    CHECK(output.world.position.norm() < 0.01);
}

void testHoldCapsAndLostContinuity() {
    PoseFilter filter;
    std::int64_t time = 0;
    constexpr double velocity = 20.0;
    Quat lastOrientation{};
    for (int i = 1; i <= 8; ++i) {
        time += ms(10.0);
        lastOrientation = fromRotationVector({0.0, 40.0 * i * 0.010, 0.0});
        (void)filter.push(sample(time, {velocity * i * 0.010, 0.0, 0.0}, lastOrientation,
                                 {velocity, 0.0, 0.0}, {0.0, 40.0, 0.0}));
    }
    CHECK(filter.state() == FilterState::Tracking);
    const Vec3 lastTrusted{velocity * 8 * 0.010, 0.0, 0.0};

    time += ms(10.0);
    (void)filter.push(sample(time, {}, {}, {}, {}, InputStatus::Invalid));
    time += ms(160.0);
    const auto lost = filter.push(sample(time, {}, {}, {}, {}, InputStatus::Invalid));
    CHECK(lost.state == FilterState::Lost);
    CHECK((lost.world.position - lastTrusted).norm() <= 0.2500001);
    CHECK(angularDistance(lost.world.orientation, lastOrientation) <= 0.7853982);

    time += ms(10.0);
    const auto stillLost = filter.push(sample(time, {}, {}, {}, {}, InputStatus::Invalid));
    CHECK((stillLost.world.position - lost.world.position).norm() < 1e-12);
    CHECK(angularDistance(stillLost.world.orientation, lost.world.orientation) < 1e-12);
}

void testLongGapRearmsInsteadOfComparing() {
    PoseFilter filter;
    std::int64_t time = warmStationary(filter);
    time += ms(100.0);
    auto output = filter.push(sample(time, {0.3, 0.0, 0.0}, {}, {3.0, 0.0, 0.0}));
    CHECK(output.state == FilterState::Cold);
    CHECK(!output.poseValid);
    CHECK((output.reasons & ReasonCallbackSilence) != 0);

    for (int i = 1; i < 8; ++i) {
        time += ms(10.0);
        output = filter.push(sample(time, {0.3 + 0.03 * i, 0.0, 0.0}, {},
                                    {3.0, 0.0, 0.0}));
    }
    CHECK(output.state == FilterState::Tracking);
    CHECK(output.poseValid);
}

void testInvalidGapStillUsesHoldWindow() {
    PoseFilter filter;
    std::int64_t time = warmStationary(filter);
    time += ms(100.0);
    const auto output = filter.push(sample(
        time, {}, {}, {}, {}, InputStatus::Invalid));
    CHECK(output.state == FilterState::Hold);
    CHECK(output.poseValid);
    CHECK(output.synthetic);
}

void testMalformedColdPoseIsForcedInvalid() {
    PoseFilter filter;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    PoseSample malformed = sample(ms(10.0));
    malformed.world.position = {nan, 0.0, 0.0};
    const auto output = filter.push(malformed);
    CHECK(output.state == FilterState::Cold);
    CHECK(!output.poseValid);
    CHECK(output.modified);
    CHECK(output.world.position.finite());
    CHECK(output.world.orientation.finite());
}

void testAccelerationRepairAndBadConfig() {
    FilterConfig config{};
    config.recoveryMaxLinearCorrectionSpeed = 0.0;
    config.recoveryMaxAngularCorrectionSpeed = std::numeric_limits<double>::quiet_NaN();
    config.holdDampingSeconds = std::numeric_limits<double>::infinity();
    PoseFilter filter(config);
    CHECK(std::isfinite(filter.config().recoveryMaxLinearCorrectionSpeed));
    CHECK(std::isfinite(filter.config().recoveryMaxAngularCorrectionSpeed));
    CHECK(std::isfinite(filter.config().holdDampingSeconds));

    std::int64_t time = warmStationary(filter);
    time += ms(10.0);
    PoseSample value = sample(time);
    value.world.linearAcceleration = {1000.0, 0.0, 0.0};
    value.world.angularAcceleration = {0.0, 10000.0, 0.0};
    const auto output = filter.push(value);
    CHECK(output.poseValid);
    CHECK(output.modified);
    CHECK(output.world.linearAcceleration.norm() == 0.0);
    CHECK(output.world.angularAcceleration.norm() == 0.0);
}

void testNoisyStationaryHasNoFalsePositive() {
    PoseFilter filter;
    std::uint32_t random = 0x5a17c9e3u;
    const auto noise = [&random]() {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return (static_cast<double>(random) / 4294967295.0) * 2.0 - 1.0;
    };

    std::int64_t time = 0;
    for (int i = 0; i < 1200; ++i) {
        time += 8'333'333;
        const Vec3 position{0.002 * noise(), 0.002 * noise(), 0.002 * noise()};
        Quat orientation = fromRotationVector(
            {0.00872664626 * noise(), 0.00872664626 * noise(), 0.00872664626 * noise()});
        if ((i & 1) != 0) orientation = -orientation;
        const auto output = filter.push(sample(time, position, orientation));
        if (i >= 7) {
            CHECK(output.state == FilterState::Tracking);
            CHECK(output.poseValid);
        }
    }
    CHECK(filter.diagnostics().rejectedSamples == 0);
}

void testSquaredGateBoundaryMatchesNormComparison() {
    // These vectors are exactly on their stated norm boundaries after IEEE-754
    // square root rounding, while their rounded squared norms are one ulp above.
    // A squared-only comparison would therefore repair/reject them incorrectly.
    const Vec3 accelerationBoundary{
        79.99999999644695, 0.0007539822368503881, 0.0};
    CHECK(accelerationBoundary.norm() == 80.0);
    CHECK(accelerationBoundary.squaredNorm() > 80.0 * 80.0);

    PoseFilter accelerationFilter;
    std::int64_t time = warmStationary(accelerationFilter);
    time += ms(10.0);
    PoseSample accelerationSample = sample(time);
    accelerationSample.world.linearAcceleration = accelerationBoundary;
    const FilterOutput accelerationOutput = accelerationFilter.push(accelerationSample);
    CHECK(accelerationOutput.state == FilterState::Tracking);
    CHECK(!accelerationOutput.modified);
    CHECK((accelerationOutput.reasons & ReasonAccelerationRepaired) == 0);
    CHECK(accelerationOutput.world.linearAcceleration.norm() == 80.0);

    FilterConfig config{};
    config.hardMaxLinearSpeed = 40.0;
    config.linearAccelerationGate = 1000.0;
    config.velocitySlack = 20.0;
    PoseFilter velocityFilter(config);
    time = warmStationary(velocityFilter);

    const Vec3 velocityBoundary{
        39.99999999995065, 0.00006283185307177003, 0.0};
    CHECK(velocityBoundary.norm() == 40.0);
    CHECK(velocityBoundary.squaredNorm() > 40.0 * 40.0);
    time += ms(50.0);
    const FilterOutput velocityOutput = velocityFilter.push(
        sample(time, {}, {}, velocityBoundary));
    CHECK(velocityOutput.state == FilterState::Tracking);
    CHECK(!velocityOutput.modified);
    CHECK((velocityOutput.reasons & ReasonVelocityRepaired) == 0);
    CHECK(velocityOutput.world.linearVelocity.norm() == 40.0);
}

void testVelocityRepairAndSilenceTick() {
    PoseFilter filter;
    std::int64_t time = warmStationary(filter);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    time += ms(10.0);
    auto output = filter.push(sample(time, {}, {}, {nan, nan, nan}, {nan, nan, nan}));
    CHECK(output.poseValid);
    CHECK(output.modified);
    CHECK(output.world.linearVelocity.finite());
    CHECK(output.world.angularVelocity.finite());

    const auto tooEarly = filter.tick(time + ms(20.0));
    CHECK(!tooEarly.has_value());
    CHECK(!filter.lastDecisionMetrics().valid);
    const auto silence = filter.tick(time + ms(30.0));
    CHECK(silence.has_value());
    CHECK(silence->state == FilterState::Hold);
    CHECK(silence->poseValid);

    time += ms(40.0);
    (void)filter.push(sample(time, {}, {}, {}, {}, InputStatus::Invalid));
    const auto freshInvalid = filter.tick(time + ms(1.0));
    CHECK(!freshInvalid.has_value());
}

void testPoseTimeOffsetDoesNotFakeSilenceOrReset() {
    // A driver that reports a steady poseTimeOffset keeps the kinematic time base
    // (timeNs) behind the steady-clock arrival time. Silence and reset detection
    // must run on arrival time, or every such tracker false-Holds between frames.
    PoseFilter filter;
    std::int64_t arrival = 0;
    for (int i = 0; i < 8; ++i) {
        arrival += ms(5.0);
        PoseSample delayed = sample(arrival - ms(30.0));
        delayed.arrivalTimeNs = arrival;
        (void)filter.push(delayed);
    }
    CHECK(filter.state() == FilterState::Tracking);
    // 5 ms after the last arrival is far below the 25 ms silence floor, even though
    // the last sample's timeNs is 35 ms in the past.
    CHECK(!filter.tick(arrival + ms(5.0)).has_value());

    // Backward jitter in the offset-bearing time base must not reset the filter.
    arrival += ms(5.0);
    PoseSample jittered = sample(arrival - ms(40.0));
    jittered.arrivalTimeNs = arrival;
    const auto output = filter.push(jittered);
    CHECK(filter.state() == FilterState::Tracking);
    CHECK((output.reasons & ReasonTimeReset) == 0);
    CHECK((output.reasons & ReasonCallbackSilence) == 0);
}

void testDisconnectIsImmediate() {
    PoseFilter filter;
    std::int64_t time = warmStationary(filter);
    time += ms(10.0);
    const auto output = filter.push(sample(time, {}, {}, {}, {}, InputStatus::Disconnected));
    CHECK(!output.connected);
    CHECK(!output.poseValid);
    CHECK(!output.synthetic);
    CHECK(filter.state() == FilterState::Cold);
}

}  // namespace

int main() {
    testMath();
    testOneFrameSpike();
    testCandidateVelocityCannotMaskSpike();
    testHighAngularAccelerationIsPoseCorroborated();
    testAngularVelocityPoseMismatchIsRepaired();
    testShortPeriodAngularVelocityPoisonIsRepaired();
    testAngularRepairDoesNotOscillateAfterStop();
    testHighRateManualRotationTraceAndPoseJump();
    testDropoutAndLoss();
    testPersistentRelocalization();
    testLegitimateMotionAndQuaternionSigns();
    testLegitimateAcceleration();
    testColdRequiresCoherentSamples();
    testUnsafeDirectReturnIsRejected();
    testHoldCapsAndLostContinuity();
    testLongGapRearmsInsteadOfComparing();
    testInvalidGapStillUsesHoldWindow();
    testMalformedColdPoseIsForcedInvalid();
    testAccelerationRepairAndBadConfig();
    testNoisyStationaryHasNoFalsePositive();
    testSquaredGateBoundaryMatchesNormComparison();
    testVelocityRepairAndSilenceTick();
    testPoseTimeOffsetDoesNotFakeSilenceOrReset();
    testDisconnectIsImmediate();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All PoseAnchor core tests passed\n";
    return EXIT_SUCCESS;
}
