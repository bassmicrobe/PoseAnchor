#pragma once

#include "pose_anchor/math.hpp"

#include <cstdint>
#include <optional>

namespace pose_anchor {

struct RigidPose {
    Vec3 position{};
    Quat orientation{};
    Vec3 linearVelocity{};
    Vec3 angularVelocity{};
    Vec3 linearAcceleration{};
    Vec3 angularAcceleration{};
};

enum class InputStatus {
    Disconnected,
    Invalid,
    RunningOk,
};

enum class FilterState {
    Cold,
    Tracking,
    Hold,
    Recovering,
    Lost,
};

enum Reason : std::uint32_t {
    ReasonNone = 0,
    ReasonUnavailable = 1u << 0,
    ReasonPositionInnovation = 1u << 1,
    ReasonRotationInnovation = 1u << 2,
    ReasonLinearSpeed = 1u << 3,
    ReasonAngularSpeed = 1u << 4,
    ReasonVelocityRepaired = 1u << 5,
    ReasonQuaternionRepaired = 1u << 6,
    ReasonCallbackSilence = 1u << 7,
    ReasonTimeReset = 1u << 8,
    ReasonAccelerationRepaired = 1u << 9,
};

struct PoseSample {
    // Kinematic time base for dt/velocity math. May carry a driver-reported
    // poseTimeOffset, so it can lag or lead the wall (steady) clock.
    std::int64_t timeNs{};
    // Steady-clock receive time. Drives callback-silence and reset-gap detection,
    // which must not see a constant poseTimeOffset as missing callbacks. Callers
    // without a separate arrival clock must set arrivalTimeNs = timeNs.
    std::int64_t arrivalTimeNs{};
    RigidPose world{};
    InputStatus status{InputStatus::Invalid};
};

struct FilterOutput {
    RigidPose world{};
    bool poseValid{};
    bool connected{true};
    bool synthetic{};
    bool modified{};
    FilterState state{FilterState::Cold};
    std::uint32_t reasons{ReasonNone};
};

struct FilterConfig {
    int warmupGoodSamples{8};
    double minDtSeconds{0.001};
    double maxKinematicDtSeconds{0.050};
    double resetGapSeconds{0.500};

    double basePositionGateMeters{0.025};
    double baseRotationGateRadians{0.13962634015954636};  // 8 degrees
    double linearAccelerationGate{80.0};
    double angularAccelerationGate{500.0};
    double velocitySlack{1.5};
    double angularVelocitySlack{4.0};
    double velocityConsistencyPositionMeters{0.005};
    double velocityConsistencyRotationRadians{0.017453292519943295};  // 1 degree
    double hardMaxLinearSpeed{25.0};
    double hardMaxAngularSpeed{60.0};

    double holdSeconds{0.150};
    double holdDampingSeconds{0.080};
    double maxHoldTranslationMeters{0.250};
    double maxHoldRotationRadians{0.7853981633974483};  // 45 degrees
    double directReturnPositionMeters{0.020};
    double directReturnRotationRadians{0.05235987755982989};  // 3 degrees
    double quickReturnPositionMeters{0.080};
    double quickReturnRotationRadians{0.2617993877991494};  // 15 degrees
    int candidateConfirmSamples{3};
    double candidateMinSpanSeconds{0.016};
    double candidateMaxPositionMeters{0.750};
    double candidateMaxRotationRadians{1.5707963267948966};  // 90 degrees
    double lostReacquireSeconds{0.080};

    double recoveryMinSeconds{0.120};
    double recoveryMaxSeconds{0.350};
    double recoveryMaxLinearCorrectionSpeed{2.5};
    double recoveryMaxAngularCorrectionSpeed{10.0};
};

struct FilterDiagnostics {
    std::uint64_t acceptedSamples{};
    std::uint64_t rejectedSamples{};
    std::uint64_t velocityRepairs{};
    std::uint64_t holdEntries{};
    std::uint64_t recoveryEntries{};
    std::uint64_t lostEntries{};
};

enum DecisionGate : std::uint32_t {
    DecisionGateNone = 0,
    DecisionGatePositionInnovation = 1u << 0,
    DecisionGateRotationInnovation = 1u << 1,
    DecisionGateLinearHardSpeed = 1u << 2,
    DecisionGateLinearVelocityDelta = 1u << 3,
    DecisionGateLinearPoseConsistency = 1u << 4,
    DecisionGateAngularHardSpeed = 1u << 5,
    DecisionGateAngularVelocityDelta = 1u << 6,
    DecisionGateAngularPoseConsistency = 1u << 7,
};

// Snapshot of the most recent pose/kinematics comparison. This is intentionally
// passive data: callers can sample it on a state transition without logging in the
// tracking hot path. `valid` is cleared at every push, tick, and reset.
struct FilterDecisionMetrics {
    bool valid{};
    bool linearVelocityRepaired{};
    bool angularVelocityRepaired{};
    double rawDtSeconds{};
    double dtSeconds{};
    double gateScale{};
    double translationMeters{};
    double rotationRadians{};
    double positionInnovationMeters{};
    double positionGateMeters{};
    double rotationInnovationRadians{};
    double rotationGateRadians{};
    double linearSpeedMetersPerSecond{};
    double hardLinearSpeedGateMetersPerSecond{};
    double angularSpeedRadiansPerSecond{};
    double hardAngularSpeedGateRadiansPerSecond{};
    double repairedLinearSpeedMetersPerSecond{};
    double repairedAngularSpeedRadiansPerSecond{};
    double linearVelocityDeltaMetersPerSecond{};
    double linearVelocityDeltaGateMetersPerSecond{};
    double angularVelocityDeltaRadiansPerSecond{};
    double angularVelocityDeltaGateRadiansPerSecond{};
    double linearPoseResidualMeters{};
    double linearPoseResidualGateMeters{};
    double angularPoseResidualRadians{};
    double angularPoseResidualGateRadians{};
    std::uint32_t exceededGates{DecisionGateNone};
    std::uint32_t rejectedGates{DecisionGateNone};
};

class PoseFilter {
public:
    explicit PoseFilter(FilterConfig config = {});

    [[nodiscard]] FilterOutput push(const PoseSample& sample);
    [[nodiscard]] std::optional<FilterOutput> tick(std::int64_t nowNs);
    void reset();

    [[nodiscard]] FilterState state() const noexcept { return state_; }
    [[nodiscard]] FilterDiagnostics diagnostics() const noexcept { return diagnostics_; }
    [[nodiscard]] FilterDecisionMetrics lastDecisionMetrics() const noexcept {
        return lastDecisionMetrics_;
    }
    [[nodiscard]] const FilterConfig& config() const noexcept { return config_; }

private:
    struct Candidate {
        bool active{};
        int count{};
        std::int64_t firstTimeNs{};
        PoseSample last{};
    };

    struct Recovery {
        std::int64_t startTimeNs{};
        double durationSeconds{};
        Vec3 positionCorrection{};
        Vec3 velocityCorrection{};
        Vec3 rotationCorrection{};
        Vec3 rotationCorrectionDerivative{};
    };

    [[nodiscard]] FilterOutput handleCold(const PoseSample& sample);
    [[nodiscard]] FilterOutput handleTracking(const PoseSample& sample);
    [[nodiscard]] FilterOutput handleHold(const PoseSample& sample);
    [[nodiscard]] FilterOutput handleRecovering(const PoseSample& sample);
    [[nodiscard]] FilterOutput handleLost(const PoseSample& sample);

    [[nodiscard]] bool prepareRunningSample(PoseSample& sample, std::uint32_t& reasons);
    [[nodiscard]] bool isOutlier(const PoseSample& previous, PoseSample& current,
                                 double gateScale, std::uint32_t& reasons);
    [[nodiscard]] RigidPose predictDamped(const RigidPose& anchor, double elapsedSeconds) const;
    [[nodiscard]] FilterOutput holdOutput(std::int64_t timeNs, std::uint32_t reasons);
    [[nodiscard]] FilterOutput rawOutput(const PoseSample& sample, bool modified,
                                         std::uint32_t reasons) const;
    [[nodiscard]] FilterOutput invalidOutput(bool connected, std::uint32_t reasons,
                                             const RigidPose& pose) const;
    [[nodiscard]] bool updateCandidate(PoseSample& sample, double gateScale,
                                       std::uint32_t& reasons);
    [[nodiscard]] bool closeTo(const RigidPose& a, const RigidPose& b,
                               double positionGate, double rotationGate) const;
    [[nodiscard]] bool safeReturnKinematics(const RigidPose& held,
                                            const PoseSample& sample) const;
    [[nodiscard]] FilterOutput beginRecovery(const PoseSample& raw, const RigidPose& currentOutput,
                                             std::uint32_t reasons);
    [[nodiscard]] FilterOutput recoveryOutput(const PoseSample& raw, std::uint32_t reasons,
                                              bool& complete) const;
    void enterHold(const RigidPose& anchor, std::int64_t anchorTimeNs);
    void clearCandidate() noexcept;
    void updateTiming(std::int64_t timeNs, std::int64_t arrivalTimeNs);

    FilterConfig config_{};
    FilterDiagnostics diagnostics_{};
    FilterDecisionMetrics lastDecisionMetrics_{};
    FilterState state_{FilterState::Cold};
    int warmupCount_{};
    bool hasTrusted_{};
    PoseSample trusted_{};
    RigidPose holdAnchor_{};
    RigidPose lostPose_{};
    std::int64_t holdAnchorTimeNs_{};
    Candidate candidate_{};
    Recovery recovery_{};
    PoseSample recoveryLastRaw_{};
    bool hasLastOutput_{};
    FilterOutput lastOutput_{};
    bool hasLastInput_{};
    std::int64_t lastInputTimeNs_{};
    std::int64_t lastArrivalTimeNs_{};
    std::int64_t lastTickTimeNs_{};
    double periodEwmaSeconds_{};
};

}  // namespace pose_anchor
