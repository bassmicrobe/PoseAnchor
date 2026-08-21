#include "pose_anchor/tracker_filter.hpp"

#include <algorithm>
#include <cmath>

namespace pose_anchor {
namespace {

constexpr double kNanosecondsPerSecond = 1'000'000'000.0;

double secondsBetween(std::int64_t newer, std::int64_t older) {
    return static_cast<double>(newer - older) / kNanosecondsPerSecond;
}

bool usableQuaternion(const Quat& q) {
    if (!q.finite()) {
        return false;
    }
    const double n2 = q.squaredNorm();
    return n2 >= 0.25 && n2 <= 2.25;
}

double stableNorm(Vec3 value) {
    return value.norm();
}

Vec3 limitMagnitude(Vec3 value, double maximum) {
    const double magnitude = stableNorm(value);
    if (!std::isfinite(magnitude)) {
        return {};
    }
    if (magnitude <= maximum || magnitude <= 1e-12) {
        return value;
    }
    return value * (maximum / magnitude);
}

}  // namespace

PoseFilter::PoseFilter(FilterConfig config) : config_(config) {
    const FilterConfig defaults{};
    const auto finiteOr = [](double value, double fallback) {
        return std::isfinite(value) ? value : fallback;
    };
    config_.warmupGoodSamples = std::clamp(config_.warmupGoodSamples, 1, 120);
    config_.candidateConfirmSamples = std::clamp(config_.candidateConfirmSamples, 2, 30);
    config_.minDtSeconds = std::clamp(finiteOr(config_.minDtSeconds, defaults.minDtSeconds),
                                      1e-5, 0.020);
    config_.maxKinematicDtSeconds = std::clamp(
        finiteOr(config_.maxKinematicDtSeconds, defaults.maxKinematicDtSeconds),
        config_.minDtSeconds, 0.200);
    config_.resetGapSeconds = std::clamp(finiteOr(config_.resetGapSeconds, defaults.resetGapSeconds),
                                         config_.maxKinematicDtSeconds, 5.0);
    config_.basePositionGateMeters = std::clamp(
        finiteOr(config_.basePositionGateMeters, defaults.basePositionGateMeters), 0.001, 0.500);
    config_.baseRotationGateRadians = std::clamp(
        finiteOr(config_.baseRotationGateRadians, defaults.baseRotationGateRadians), 0.001, 3.141592653589793);
    config_.linearAccelerationGate = std::clamp(
        finiteOr(config_.linearAccelerationGate, defaults.linearAccelerationGate), 1.0, 1000.0);
    config_.angularAccelerationGate = std::clamp(
        finiteOr(config_.angularAccelerationGate, defaults.angularAccelerationGate), 1.0, 10000.0);
    config_.velocitySlack = std::clamp(finiteOr(config_.velocitySlack, defaults.velocitySlack), 0.0, 20.0);
    config_.angularVelocitySlack = std::clamp(
        finiteOr(config_.angularVelocitySlack, defaults.angularVelocitySlack), 0.0, 100.0);
    config_.velocityConsistencyPositionMeters = std::clamp(
        finiteOr(config_.velocityConsistencyPositionMeters,
                 defaults.velocityConsistencyPositionMeters), 0.0005, 0.250);
    config_.velocityConsistencyRotationRadians = std::clamp(
        finiteOr(config_.velocityConsistencyRotationRadians,
                 defaults.velocityConsistencyRotationRadians), 0.001, 1.0);
    config_.hardMaxLinearSpeed = std::clamp(
        finiteOr(config_.hardMaxLinearSpeed, defaults.hardMaxLinearSpeed), 0.5, 50.0);
    config_.hardMaxAngularSpeed = std::clamp(
        finiteOr(config_.hardMaxAngularSpeed, defaults.hardMaxAngularSpeed), 1.0, 150.0);
    config_.holdDampingSeconds = std::clamp(
        finiteOr(config_.holdDampingSeconds, defaults.holdDampingSeconds), 0.005, 0.500);
    config_.holdSeconds = std::clamp(
        finiteOr(config_.holdSeconds, defaults.holdSeconds), 0.010, 0.250);
    config_.maxHoldTranslationMeters = std::clamp(
        finiteOr(config_.maxHoldTranslationMeters, defaults.maxHoldTranslationMeters), 0.010, 1.000);
    config_.maxHoldRotationRadians = std::clamp(
        finiteOr(config_.maxHoldRotationRadians, defaults.maxHoldRotationRadians), 0.05, 3.141592653589793);
    config_.directReturnPositionMeters = std::clamp(
        finiteOr(config_.directReturnPositionMeters, defaults.directReturnPositionMeters), 0.001, 0.250);
    config_.directReturnRotationRadians = std::clamp(
        finiteOr(config_.directReturnRotationRadians, defaults.directReturnRotationRadians), 0.001, 1.0);
    config_.quickReturnPositionMeters = std::clamp(
        finiteOr(config_.quickReturnPositionMeters, defaults.quickReturnPositionMeters),
        config_.directReturnPositionMeters, 1.0);
    config_.quickReturnRotationRadians = std::clamp(
        finiteOr(config_.quickReturnRotationRadians, defaults.quickReturnRotationRadians),
        config_.directReturnRotationRadians, 3.141592653589793);
    config_.candidateMinSpanSeconds = std::clamp(
        finiteOr(config_.candidateMinSpanSeconds, defaults.candidateMinSpanSeconds), 0.0, 0.200);
    config_.candidateMaxPositionMeters = std::clamp(
        finiteOr(config_.candidateMaxPositionMeters, defaults.candidateMaxPositionMeters), 0.05, 2.0);
    config_.candidateMaxRotationRadians = std::clamp(
        finiteOr(config_.candidateMaxRotationRadians, defaults.candidateMaxRotationRadians), 0.1, 3.141592653589793);
    config_.lostReacquireSeconds = std::clamp(
        finiteOr(config_.lostReacquireSeconds, defaults.lostReacquireSeconds), 0.010, 0.500);
    config_.recoveryMinSeconds = std::clamp(
        finiteOr(config_.recoveryMinSeconds, defaults.recoveryMinSeconds), 0.010, 0.500);
    config_.recoveryMaxSeconds = std::clamp(
        finiteOr(config_.recoveryMaxSeconds, defaults.recoveryMaxSeconds),
        config_.recoveryMinSeconds, 1.0);
    config_.recoveryMaxLinearCorrectionSpeed = std::clamp(
        finiteOr(config_.recoveryMaxLinearCorrectionSpeed,
                 defaults.recoveryMaxLinearCorrectionSpeed), 0.1, 20.0);
    config_.recoveryMaxAngularCorrectionSpeed = std::clamp(
        finiteOr(config_.recoveryMaxAngularCorrectionSpeed,
                 defaults.recoveryMaxAngularCorrectionSpeed), 0.1, 100.0);
}

void PoseFilter::reset() {
    state_ = FilterState::Cold;
    warmupCount_ = 0;
    hasTrusted_ = false;
    trusted_ = {};
    holdAnchor_ = {};
    lostPose_ = {};
    holdAnchorTimeNs_ = 0;
    clearCandidate();
    recovery_ = {};
    recoveryLastRaw_ = {};
    hasLastOutput_ = false;
    lastOutput_ = {};
    hasLastInput_ = false;
    lastInputTimeNs_ = 0;
    lastArrivalTimeNs_ = 0;
    lastTickTimeNs_ = 0;
    periodEwmaSeconds_ = 0.0;
    lastDecisionMetrics_ = {};
}

FilterOutput PoseFilter::push(const PoseSample& input) {
    lastDecisionMetrics_.valid = false;
    if (input.status == InputStatus::Disconnected) {
        reset();
        FilterOutput output{};
        output.world = input.world;
        output.poseValid = false;
        output.connected = false;
        output.state = FilterState::Cold;
        output.reasons = ReasonUnavailable;
        return output;
    }

    bool timeReset = false;
    bool gapReacquire = false;
    if (hasLastInput_) {
        // Arrival time is monotonic; a constant or jittering poseTimeOffset in the
        // kinematic time base must not read as a callback gap or clock reset.
        const double delta = secondsBetween(input.arrivalTimeNs, lastArrivalTimeNs_);
        if (delta < -0.002 || delta > config_.resetGapSeconds) {
            reset();
            timeReset = true;
        } else if (input.status == InputStatus::RunningOk &&
                   delta > config_.maxKinematicDtSeconds &&
                   (state_ == FilterState::Tracking || state_ == FilterState::Recovering)) {
            reset();
            gapReacquire = true;
        }
    }
    updateTiming(input.timeNs, input.arrivalTimeNs);

    FilterOutput output{};
    switch (state_) {
        case FilterState::Cold: output = handleCold(input); break;
        case FilterState::Tracking: output = handleTracking(input); break;
        case FilterState::Hold: output = handleHold(input); break;
        case FilterState::Recovering: output = handleRecovering(input); break;
        case FilterState::Lost: output = handleLost(input); break;
    }

    if (timeReset) {
        output.reasons |= ReasonTimeReset;
    }
    if (gapReacquire) {
        output.reasons |= ReasonCallbackSilence;
    }
    lastOutput_ = output;
    hasLastOutput_ = true;
    return output;
}

std::optional<FilterOutput> PoseFilter::tick(std::int64_t nowNs) {
    lastDecisionMetrics_.valid = false;
    if (!hasLastInput_ || state_ == FilterState::Cold) {
        return std::nullopt;
    }

    const double silenceThreshold = std::max(0.025, 2.5 * periodEwmaSeconds_);
    if (secondsBetween(nowNs, lastArrivalTimeNs_) < silenceThreshold) {
        return std::nullopt;
    }
    if (lastTickTimeNs_ != 0 && secondsBetween(nowNs, lastTickTimeNs_) < 0.002) {
        return std::nullopt;
    }
    lastTickTimeNs_ = nowNs;

    if (state_ == FilterState::Tracking || state_ == FilterState::Recovering) {
        const RigidPose anchor = hasLastOutput_ ? lastOutput_.world : trusted_.world;
        enterHold(anchor, lastInputTimeNs_);
    }

    FilterOutput output = holdOutput(nowNs, ReasonCallbackSilence | ReasonUnavailable);
    lastOutput_ = output;
    hasLastOutput_ = true;
    return output;
}

FilterOutput PoseFilter::handleCold(const PoseSample& input) {
    if (input.status != InputStatus::RunningOk) {
        warmupCount_ = 0;
        return invalidOutput(true, ReasonUnavailable, input.world);
    }

    PoseSample sample = input;
    std::uint32_t reasons = ReasonNone;
    if (!prepareRunningSample(sample, reasons)) {
        warmupCount_ = 0;
        const RigidPose safePose = hasTrusted_ ? trusted_.world : RigidPose{};
        FilterOutput output = invalidOutput(true, ReasonUnavailable | reasons, safePose);
        // A malformed pose advertised as Running_OK must not fail open as valid raw data.
        output.modified = true;
        return output;
    }

    if (warmupCount_ > 0) {
        std::uint32_t coherenceReasons = ReasonNone;
        if (isOutlier(trusted_, sample, 1.5, coherenceReasons)) {
            warmupCount_ = 1;
            trusted_ = sample;
            hasTrusted_ = true;
            ++diagnostics_.rejectedSamples;
            FilterOutput waiting{};
            waiting.world = sample.world;
            waiting.connected = true;
            waiting.modified = true;
            waiting.state = FilterState::Cold;
            waiting.reasons = reasons | coherenceReasons;
            return waiting;
        }
        reasons |= coherenceReasons;
    }

    trusted_ = sample;
    hasTrusted_ = true;
    ++warmupCount_;
    if (warmupCount_ >= config_.warmupGoodSamples) {
        state_ = FilterState::Tracking;
        ++diagnostics_.acceptedSamples;
        return rawOutput(sample, reasons != ReasonNone, reasons);
    }
    FilterOutput waiting{};
    waiting.world = sample.world;
    waiting.connected = true;
    waiting.modified = true;
    waiting.state = FilterState::Cold;
    waiting.reasons = reasons;
    return waiting;
}

FilterOutput PoseFilter::handleTracking(const PoseSample& input) {
    if (input.status != InputStatus::RunningOk) {
        enterHold(trusted_.world, trusted_.timeNs);
        return holdOutput(input.timeNs, ReasonUnavailable);
    }

    PoseSample sample = input;
    std::uint32_t reasons = ReasonNone;
    if (!prepareRunningSample(sample, reasons)) {
        enterHold(trusted_.world, trusted_.timeNs);
        return holdOutput(input.timeNs, ReasonUnavailable | reasons);
    }

    std::uint32_t outlierReasons = ReasonNone;
    if (isOutlier(trusted_, sample, 1.0, outlierReasons)) {
        ++diagnostics_.rejectedSamples;
        enterHold(trusted_.world, trusted_.timeNs);
        return holdOutput(sample.timeNs, reasons | outlierReasons);
    }
    reasons |= outlierReasons;

    trusted_ = sample;
    ++diagnostics_.acceptedSamples;
    return rawOutput(sample, reasons != ReasonNone, reasons);
}

FilterOutput PoseFilter::handleHold(const PoseSample& input) {
    if (input.status != InputStatus::RunningOk) {
        return holdOutput(input.timeNs, ReasonUnavailable);
    }

    PoseSample sample = input;
    std::uint32_t reasons = ReasonNone;
    if (!prepareRunningSample(sample, reasons)) {
        return holdOutput(input.timeNs, ReasonUnavailable | reasons);
    }

    const double elapsed = std::max(0.0, secondsBetween(sample.timeNs, holdAnchorTimeNs_));
    const RigidPose held = predictDamped(holdAnchor_, std::min(elapsed, config_.holdSeconds));

    if (closeTo(held, sample.world, config_.directReturnPositionMeters,
                config_.directReturnRotationRadians) && safeReturnKinematics(held, sample)) {
        state_ = FilterState::Tracking;
        trusted_ = sample;
        hasTrusted_ = true;
        clearCandidate();
        ++diagnostics_.acceptedSamples;
        return rawOutput(sample, reasons != ReasonNone, reasons);
    }

    const bool coherent = updateCandidate(sample, 1.5, reasons);
    const double candidateSpan = candidate_.active
        ? secondsBetween(candidate_.last.timeNs, candidate_.firstTimeNs)
        : 0.0;
    const bool quick = closeTo(held, sample.world, config_.quickReturnPositionMeters,
                               config_.quickReturnRotationRadians) && candidate_.count >= 2;
    const bool confirmed = coherent && candidate_.count >= config_.candidateConfirmSamples &&
                           candidateSpan >= config_.candidateMinSpanSeconds;
    const bool insideRecoveryEnvelope =
        closeTo(held, sample.world, config_.candidateMaxPositionMeters,
                config_.candidateMaxRotationRadians);

    if ((quick || confirmed) && insideRecoveryEnvelope) {
        return beginRecovery(sample, held, reasons);
    }

    return holdOutput(input.timeNs, reasons);
}

FilterOutput PoseFilter::handleRecovering(const PoseSample& input) {
    if (input.status != InputStatus::RunningOk) {
        const RigidPose anchor = hasLastOutput_ ? lastOutput_.world : holdAnchor_;
        enterHold(anchor, input.timeNs);
        return holdOutput(input.timeNs, ReasonUnavailable);
    }

    PoseSample sample = input;
    std::uint32_t reasons = ReasonNone;
    if (!prepareRunningSample(sample, reasons)) {
        const RigidPose anchor = hasLastOutput_ ? lastOutput_.world : holdAnchor_;
        enterHold(anchor, input.timeNs);
        return holdOutput(input.timeNs, ReasonUnavailable | reasons);
    }

    std::uint32_t outlierReasons = ReasonNone;
    if (isOutlier(recoveryLastRaw_, sample, 1.5, outlierReasons)) {
        ++diagnostics_.rejectedSamples;
        const RigidPose anchor = hasLastOutput_ ? lastOutput_.world : holdAnchor_;
        enterHold(anchor, input.timeNs);
        return holdOutput(input.timeNs, reasons | outlierReasons);
    }
    reasons |= outlierReasons;

    bool complete = false;
    FilterOutput output = recoveryOutput(sample, reasons, complete);
    recoveryLastRaw_ = sample;
    if (complete) {
        state_ = FilterState::Tracking;
        trusted_ = sample;
        hasTrusted_ = true;
        ++diagnostics_.acceptedSamples;
        return rawOutput(sample, reasons != ReasonNone, reasons);
    }
    return output;
}

FilterOutput PoseFilter::handleLost(const PoseSample& input) {
    if (input.status != InputStatus::RunningOk) {
        return invalidOutput(true, ReasonUnavailable, lostPose_);
    }

    PoseSample sample = input;
    std::uint32_t reasons = ReasonNone;
    if (!prepareRunningSample(sample, reasons)) {
        return invalidOutput(true, ReasonUnavailable | reasons, lostPose_);
    }

    (void)updateCandidate(sample, 1.5, reasons);
    const double span = candidate_.active
        ? secondsBetween(candidate_.last.timeNs, candidate_.firstTimeNs)
        : 0.0;
    if (candidate_.count >= config_.candidateConfirmSamples && span >= config_.lostReacquireSeconds) {
        state_ = FilterState::Tracking;
        trusted_ = sample;
        hasTrusted_ = true;
        clearCandidate();
        ++diagnostics_.acceptedSamples;
        return rawOutput(sample, reasons != ReasonNone, reasons);
    }
    return invalidOutput(true, reasons, lostPose_);
}

bool PoseFilter::prepareRunningSample(PoseSample& sample, std::uint32_t& reasons) {
    if (!sample.world.position.finite() || !usableQuaternion(sample.world.orientation)) {
        return false;
    }

    const double originalNorm = std::sqrt(sample.world.orientation.squaredNorm());
    sample.world.orientation = normalized(sample.world.orientation);
    if (std::abs(originalNorm - 1.0) > 1e-6) {
        reasons |= ReasonQuaternionRepaired;
    }
    if (hasTrusted_) {
        sample.world.orientation = sameHemisphere(sample.world.orientation, trusted_.world.orientation);
    }

    bool repairedVelocity = false;
    if (!sample.world.linearVelocity.finite() || !sample.world.angularVelocity.finite()) {
        repairedVelocity = true;
        const PoseSample* reference = hasTrusted_ ? &trusted_ : nullptr;
        if ((state_ == FilterState::Hold || state_ == FilterState::Lost) && candidate_.active) {
            reference = &candidate_.last;
        } else if (state_ == FilterState::Recovering) {
            reference = &recoveryLastRaw_;
        }
        if (reference) {
            const double dt = std::clamp(secondsBetween(sample.timeNs, reference->timeNs),
                                         config_.minDtSeconds, config_.maxKinematicDtSeconds);
            if (!sample.world.linearVelocity.finite()) {
                sample.world.linearVelocity = (sample.world.position - reference->world.position) / dt;
            }
            if (!sample.world.angularVelocity.finite()) {
                const Quat delta = sample.world.orientation * conjugate(reference->world.orientation);
                sample.world.angularVelocity = rotationVector(delta) / dt;
            }
        } else {
            if (!sample.world.linearVelocity.finite()) sample.world.linearVelocity = {};
            if (!sample.world.angularVelocity.finite()) sample.world.angularVelocity = {};
        }
    }
    bool repairedAcceleration = false;
    if (!sample.world.linearAcceleration.finite() ||
        sample.world.linearAcceleration.norm() > config_.linearAccelerationGate) {
        sample.world.linearAcceleration = {};
        repairedAcceleration = true;
    }
    if (!sample.world.angularAcceleration.finite() ||
        sample.world.angularAcceleration.norm() > config_.angularAccelerationGate) {
        sample.world.angularAcceleration = {};
        repairedAcceleration = true;
    }
    if (repairedVelocity) {
        reasons |= ReasonVelocityRepaired;
        ++diagnostics_.velocityRepairs;
    }
    if (repairedAcceleration) reasons |= ReasonAccelerationRepaired;
    return true;
}

bool PoseFilter::isOutlier(const PoseSample& previous, PoseSample& current,
                           double gateScale, std::uint32_t& reasons) {
    const double rawDt = secondsBetween(current.timeNs, previous.timeNs);
    const double dt = std::clamp(rawDt,
                                 config_.minDtSeconds, config_.maxKinematicDtSeconds);
    const Vec3 displacement = current.world.position - previous.world.position;
    // Only trusted state may predict the candidate. A bogus candidate velocity must not
    // explain away its own position jump.
    const Vec3 expectedDisplacement = previous.world.linearVelocity * dt;
    const Vec3 positionInnovation = displacement - expectedDisplacement;
    const Vec3 velocityPoseResidual = displacement -
        0.5 * (previous.world.linearVelocity + current.world.linearVelocity) * dt;

    const Quat delta = current.world.orientation * conjugate(previous.world.orientation);
    const Vec3 rotation = rotationVector(delta);
    const Vec3 expectedRotation = previous.world.angularVelocity * dt;
    const Vec3 rotationInnovation = rotation - expectedRotation;
    const Vec3 angularVelocityPoseResidual = rotation -
        0.5 * (previous.world.angularVelocity + current.world.angularVelocity) * dt;

    const double positionGate = gateScale *
        (config_.basePositionGateMeters + config_.velocitySlack * dt +
         0.5 * config_.linearAccelerationGate * dt * dt);
    const double rotationGate = gateScale *
        (config_.baseRotationGateRadians + config_.angularVelocitySlack * dt +
         0.5 * config_.angularAccelerationGate * dt * dt);
    const double velocityConsistencyGate = gateScale *
        (config_.velocityConsistencyPositionMeters + config_.velocitySlack * dt +
         0.5 * config_.linearAccelerationGate * dt * dt);
    const double angularVelocityConsistencyGate = gateScale *
        (config_.velocityConsistencyRotationRadians + config_.angularVelocitySlack * dt +
         0.5 * config_.angularAccelerationGate * dt * dt);

    const double translation = stableNorm(displacement);
    const double rotationAmount = stableNorm(rotation);
    const double positionInnovationAmount = stableNorm(positionInnovation);
    const double rotationInnovationAmount = stableNorm(rotationInnovation);
    const double linearSpeed = stableNorm(current.world.linearVelocity);
    const double angularSpeed = stableNorm(current.world.angularVelocity);
    const double linearDelta = stableNorm(
        current.world.linearVelocity - previous.world.linearVelocity);
    const double angularDelta = stableNorm(
        current.world.angularVelocity - previous.world.angularVelocity);
    const double linearVelocityPoseResidual = stableNorm(velocityPoseResidual);
    const double angularVelocityPoseResidualAmount = stableNorm(angularVelocityPoseResidual);
    const double linearDeltaGate = config_.linearAccelerationGate * dt + config_.velocitySlack;
    const double angularDeltaGate =
        config_.angularAccelerationGate * dt + config_.angularVelocitySlack;

    const bool positionInnovationExceeded = positionInnovationAmount > positionGate;
    const bool rotationInnovationExceeded = rotationInnovationAmount > rotationGate;
    const bool linearHardSpeedExceeded = linearSpeed > config_.hardMaxLinearSpeed;
    const bool linearDeltaExceeded = linearDelta > linearDeltaGate;
    const bool linearPoseConsistencyExceeded =
        linearVelocityPoseResidual > velocityConsistencyGate;
    const bool angularHardSpeedExceeded = angularSpeed > config_.hardMaxAngularSpeed;
    const bool angularDeltaExceeded = angularDelta > angularDeltaGate;
    const bool angularPoseConsistencyExceeded =
        angularVelocityPoseResidualAmount > angularVelocityConsistencyGate;

    lastDecisionMetrics_ = {};
    lastDecisionMetrics_.valid = true;
    lastDecisionMetrics_.rawDtSeconds = rawDt;
    lastDecisionMetrics_.dtSeconds = dt;
    lastDecisionMetrics_.gateScale = gateScale;
    lastDecisionMetrics_.translationMeters = translation;
    lastDecisionMetrics_.rotationRadians = rotationAmount;
    lastDecisionMetrics_.positionInnovationMeters = positionInnovationAmount;
    lastDecisionMetrics_.positionGateMeters = positionGate;
    lastDecisionMetrics_.rotationInnovationRadians = rotationInnovationAmount;
    lastDecisionMetrics_.rotationGateRadians = rotationGate;
    lastDecisionMetrics_.linearSpeedMetersPerSecond = linearSpeed;
    lastDecisionMetrics_.hardLinearSpeedGateMetersPerSecond = config_.hardMaxLinearSpeed;
    lastDecisionMetrics_.angularSpeedRadiansPerSecond = angularSpeed;
    lastDecisionMetrics_.hardAngularSpeedGateRadiansPerSecond = config_.hardMaxAngularSpeed;
    lastDecisionMetrics_.repairedLinearSpeedMetersPerSecond = linearSpeed;
    lastDecisionMetrics_.repairedAngularSpeedRadiansPerSecond = angularSpeed;
    lastDecisionMetrics_.linearVelocityDeltaMetersPerSecond = linearDelta;
    lastDecisionMetrics_.linearVelocityDeltaGateMetersPerSecond = linearDeltaGate;
    lastDecisionMetrics_.angularVelocityDeltaRadiansPerSecond = angularDelta;
    lastDecisionMetrics_.angularVelocityDeltaGateRadiansPerSecond = angularDeltaGate;
    lastDecisionMetrics_.linearPoseResidualMeters = linearVelocityPoseResidual;
    lastDecisionMetrics_.linearPoseResidualGateMeters = velocityConsistencyGate;
    lastDecisionMetrics_.angularPoseResidualRadians = angularVelocityPoseResidualAmount;
    lastDecisionMetrics_.angularPoseResidualGateRadians = angularVelocityConsistencyGate;

    const auto recordExceeded = [this](bool exceeded, DecisionGate gate) {
        if (exceeded) {
            lastDecisionMetrics_.exceededGates |= static_cast<std::uint32_t>(gate);
        }
    };
    recordExceeded(positionInnovationExceeded, DecisionGatePositionInnovation);
    recordExceeded(rotationInnovationExceeded, DecisionGateRotationInnovation);
    recordExceeded(linearHardSpeedExceeded, DecisionGateLinearHardSpeed);
    recordExceeded(linearDeltaExceeded, DecisionGateLinearVelocityDelta);
    recordExceeded(linearPoseConsistencyExceeded, DecisionGateLinearPoseConsistency);
    recordExceeded(angularHardSpeedExceeded, DecisionGateAngularHardSpeed);
    recordExceeded(angularDeltaExceeded, DecisionGateAngularVelocityDelta);
    recordExceeded(angularPoseConsistencyExceeded, DecisionGateAngularPoseConsistency);

    bool outlier = false;
    if (positionInnovationExceeded) {
        reasons |= ReasonPositionInnovation;
        lastDecisionMetrics_.rejectedGates |= DecisionGatePositionInnovation;
        outlier = true;
    }
    if (rotationInnovationExceeded) {
        reasons |= ReasonRotationInnovation;
        lastDecisionMetrics_.rejectedGates |= DecisionGateRotationInnovation;
        outlier = true;
    }

    // Reported velocities are auxiliary kinematics, while position/orientation are
    // the tracking measurement. A velocity-only anomaly must not make a coherent
    // pose enter Hold. Replace the affected component with the finite-difference
    // velocity supported by this pose interval, bounded so repair can never increase
    // the reported speed or exceed the configured hard maximum. Using the interval
    // velocity (rather than an inferred endpoint) also avoids sign-flip oscillation
    // when a moving tracker stops. Pose innovations above still reject the whole sample.
    bool repairedVelocity = false;
    if (!outlier &&
        (linearHardSpeedExceeded || linearDeltaExceeded || linearPoseConsistencyExceeded)) {
        const Vec3 poseIntervalVelocity = displacement / dt;
        current.world.linearVelocity = limitMagnitude(
            poseIntervalVelocity, std::min(linearSpeed, config_.hardMaxLinearSpeed));
        lastDecisionMetrics_.linearVelocityRepaired = true;
        lastDecisionMetrics_.repairedLinearSpeedMetersPerSecond =
            stableNorm(current.world.linearVelocity);
        repairedVelocity = true;
    }
    if (!outlier &&
        (angularHardSpeedExceeded || angularDeltaExceeded || angularPoseConsistencyExceeded)) {
        const Vec3 poseIntervalAngularVelocity = rotation / dt;
        current.world.angularVelocity = limitMagnitude(
            poseIntervalAngularVelocity, std::min(angularSpeed, config_.hardMaxAngularSpeed));
        lastDecisionMetrics_.angularVelocityRepaired = true;
        lastDecisionMetrics_.repairedAngularSpeedRadiansPerSecond =
            stableNorm(current.world.angularVelocity);
        repairedVelocity = true;
    }
    if (repairedVelocity) {
        reasons |= ReasonVelocityRepaired;
        ++diagnostics_.velocityRepairs;
    }
    return outlier;
}

RigidPose PoseFilter::predictDamped(const RigidPose& anchor, double elapsedSeconds) const {
    const double t = std::max(0.0, elapsedSeconds);
    const double tau = config_.holdDampingSeconds;
    const double decay = std::exp(-t / tau);
    const double distanceScale = tau * (1.0 - decay);
    RigidPose output = anchor;
    Vec3 translation = anchor.linearVelocity * distanceScale;
    Vec3 rotation = anchor.angularVelocity * distanceScale;
    bool translationCapped = false;
    bool rotationCapped = false;
    if (translation.norm() > config_.maxHoldTranslationMeters) {
        translation = translation * (config_.maxHoldTranslationMeters / translation.norm());
        translationCapped = true;
    }
    if (rotation.norm() > config_.maxHoldRotationRadians) {
        rotation = rotation * (config_.maxHoldRotationRadians / rotation.norm());
        rotationCapped = true;
    }
    output.position = anchor.position + translation;
    output.orientation = normalized(fromRotationVector(rotation) * anchor.orientation);
    output.linearVelocity = anchor.linearVelocity * decay;
    output.angularVelocity = anchor.angularVelocity * decay;
    if (translationCapped) output.linearVelocity = {};
    if (rotationCapped) output.angularVelocity = {};
    output.linearAcceleration = {};
    output.angularAcceleration = {};
    return output;
}

FilterOutput PoseFilter::holdOutput(std::int64_t timeNs, std::uint32_t reasons) {
    const double elapsed = std::max(0.0, secondsBetween(timeNs, holdAnchorTimeNs_));
    if (elapsed > config_.holdSeconds) {
        if (state_ != FilterState::Lost) {
            state_ = FilterState::Lost;
            clearCandidate();
            ++diagnostics_.lostEntries;
        }
        lostPose_ = predictDamped(holdAnchor_, config_.holdSeconds);
        return invalidOutput(true, reasons | ReasonUnavailable, lostPose_);
    }

    FilterOutput output{};
    output.world = predictDamped(holdAnchor_, elapsed);
    output.poseValid = true;
    output.connected = true;
    output.synthetic = true;
    output.modified = true;
    output.state = FilterState::Hold;
    output.reasons = reasons;
    return output;
}

FilterOutput PoseFilter::rawOutput(const PoseSample& sample, bool modified,
                                   std::uint32_t reasons) const {
    FilterOutput output{};
    output.world = sample.world;
    output.poseValid = true;
    output.connected = true;
    output.synthetic = false;
    output.modified = modified;
    output.state = state_;
    output.reasons = reasons;
    return output;
}

FilterOutput PoseFilter::invalidOutput(bool connected, std::uint32_t reasons,
                                       const RigidPose& pose) const {
    FilterOutput output{};
    output.world = pose;
    output.poseValid = false;
    output.connected = connected;
    output.synthetic = state_ == FilterState::Hold;
    output.modified = state_ == FilterState::Hold || state_ == FilterState::Lost;
    output.state = state_;
    output.reasons = reasons;
    return output;
}

bool PoseFilter::updateCandidate(PoseSample& sample, double gateScale,
                                 std::uint32_t& reasons) {
    if (!candidate_.active) {
        candidate_.active = true;
        candidate_.count = 1;
        candidate_.firstTimeNs = sample.timeNs;
        candidate_.last = sample;
        return true;
    }

    std::uint32_t candidateReasons = ReasonNone;
    if (isOutlier(candidate_.last, sample, gateScale, candidateReasons)) {
        reasons |= candidateReasons;
        candidate_.count = 1;
        candidate_.firstTimeNs = sample.timeNs;
        candidate_.last = sample;
        return false;
    }
    reasons |= candidateReasons;
    ++candidate_.count;
    candidate_.last = sample;
    return true;
}

bool PoseFilter::closeTo(const RigidPose& a, const RigidPose& b,
                         double positionGate, double rotationGate) const {
    return (a.position - b.position).norm() <= positionGate &&
           angularDistance(a.orientation, b.orientation) <= rotationGate;
}

bool PoseFilter::safeReturnKinematics(const RigidPose& held, const PoseSample& sample) const {
    const double dt = std::clamp(secondsBetween(sample.timeNs, holdAnchorTimeNs_),
                                 config_.minDtSeconds, config_.maxKinematicDtSeconds);
    return sample.world.linearVelocity.norm() <= config_.hardMaxLinearSpeed &&
           sample.world.angularVelocity.norm() <= config_.hardMaxAngularSpeed &&
           (sample.world.linearVelocity - held.linearVelocity).norm() <=
               config_.linearAccelerationGate * dt + config_.velocitySlack &&
           (sample.world.angularVelocity - held.angularVelocity).norm() <=
               config_.angularAccelerationGate * dt + config_.angularVelocitySlack;
}

FilterOutput PoseFilter::beginRecovery(const PoseSample& raw, const RigidPose& currentOutput,
                                       std::uint32_t reasons) {
    state_ = FilterState::Recovering;
    clearCandidate();
    ++diagnostics_.recoveryEntries;

    recovery_.startTimeNs = raw.timeNs;
    recovery_.positionCorrection = currentOutput.position - raw.world.position;
    recovery_.velocityCorrection = currentOutput.linearVelocity - raw.world.linearVelocity;

    Quat correction = normalized(currentOutput.orientation * conjugate(raw.world.orientation));
    if (correction.w < 0.0) correction = -correction;
    recovery_.rotationCorrection = rotationVector(correction);
    const Vec3 angularCorrection = currentOutput.angularVelocity -
                                   rotate(correction, raw.world.angularVelocity);
    recovery_.rotationCorrectionDerivative =
        leftJacobianInverseApply(recovery_.rotationCorrection, angularCorrection);

    const double positionTime = 1.5 * recovery_.positionCorrection.norm() /
                                config_.recoveryMaxLinearCorrectionSpeed;
    const double rotationTime = 1.5 * recovery_.rotationCorrection.norm() /
                                config_.recoveryMaxAngularCorrectionSpeed;
    const double velocityTime = recovery_.velocityCorrection.norm() /
                                config_.linearAccelerationGate;
    recovery_.durationSeconds = std::clamp(
        std::max({config_.recoveryMinSeconds, positionTime, rotationTime, velocityTime}),
        config_.recoveryMinSeconds, config_.recoveryMaxSeconds);
    recoveryLastRaw_ = raw;

    bool complete = false;
    return recoveryOutput(raw, reasons, complete);
}

FilterOutput PoseFilter::recoveryOutput(const PoseSample& raw, std::uint32_t reasons,
                                        bool& complete) const {
    const double elapsed = std::max(0.0, secondsBetween(raw.timeNs, recovery_.startTimeNs));
    if (elapsed >= recovery_.durationSeconds) {
        complete = true;
        return rawOutput(raw, reasons != ReasonNone, reasons);
    }

    complete = false;
    const double duration = recovery_.durationSeconds;
    const double u = std::clamp(elapsed / duration, 0.0, 1.0);
    const double u2 = u * u;
    const double u3 = u2 * u;
    const double h00 = 2.0 * u3 - 3.0 * u2 + 1.0;
    const double h10 = u3 - 2.0 * u2 + u;
    const double dh00 = (6.0 * u2 - 6.0 * u) / duration;
    const double dh10 = 3.0 * u2 - 4.0 * u + 1.0;

    const Vec3 positionCorrection = h00 * recovery_.positionCorrection +
                                    (h10 * duration) * recovery_.velocityCorrection;
    const Vec3 velocityCorrection = dh00 * recovery_.positionCorrection +
                                    dh10 * recovery_.velocityCorrection;
    const Vec3 rotationCorrection = h00 * recovery_.rotationCorrection +
                                    (h10 * duration) * recovery_.rotationCorrectionDerivative;
    const Vec3 rotationDerivative = dh00 * recovery_.rotationCorrection +
                                    dh10 * recovery_.rotationCorrectionDerivative;
    const Quat correction = fromRotationVector(rotationCorrection);
    const Vec3 correctionAngularVelocity =
        leftJacobianApply(rotationCorrection, rotationDerivative);

    FilterOutput output{};
    output.world = raw.world;
    output.world.position = raw.world.position + positionCorrection;
    output.world.linearVelocity = raw.world.linearVelocity + velocityCorrection;
    output.world.orientation = normalized(correction * raw.world.orientation);
    output.world.angularVelocity = correctionAngularVelocity +
                                   rotate(correction, raw.world.angularVelocity);
    output.world.linearAcceleration = {};
    output.world.angularAcceleration = {};
    output.poseValid = true;
    output.connected = true;
    output.synthetic = true;
    output.modified = true;
    output.state = FilterState::Recovering;
    output.reasons = reasons;
    return output;
}

void PoseFilter::enterHold(const RigidPose& anchor, std::int64_t anchorTimeNs) {
    state_ = FilterState::Hold;
    holdAnchor_ = anchor;
    holdAnchorTimeNs_ = anchorTimeNs;
    clearCandidate();
    ++diagnostics_.holdEntries;
}

void PoseFilter::clearCandidate() noexcept {
    candidate_ = {};
}

void PoseFilter::updateTiming(std::int64_t timeNs, std::int64_t arrivalTimeNs) {
    if (hasLastInput_) {
        const double dt = secondsBetween(arrivalTimeNs, lastArrivalTimeNs_);
        if (dt > 0.0 && dt < config_.resetGapSeconds) {
            periodEwmaSeconds_ = periodEwmaSeconds_ == 0.0
                ? dt
                : 0.9 * periodEwmaSeconds_ + 0.1 * dt;
        }
    }
    hasLastInput_ = true;
    lastInputTimeNs_ = timeNs;
    lastArrivalTimeNs_ = arrivalTimeNs;
    lastTickTimeNs_ = 0;
}

}  // namespace pose_anchor
