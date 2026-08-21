#include "server_provider.hpp"

#include "hooks.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

// The build stamps the version from CMakeLists.txt (the single source of truth).
#ifndef POSE_ANCHOR_VERSION
#define POSE_ANCHOR_VERSION "unversioned"
#endif

namespace pose_anchor::driver {
namespace {

constexpr const char* kSettingsSection = "driver_pose_anchor";
// Applied independently per device and destination state so the first Hold is
// never hidden by a nearby warm-up or recovery transition.
constexpr std::int64_t kTransitionLogIntervalNs = 1'000'000'000;

const char* stateName(FilterState state) {
    switch (state) {
        case FilterState::Cold: return "cold";
        case FilterState::Tracking: return "tracking";
        case FilterState::Hold: return "hold";
        case FilterState::Recovering: return "recovering";
        case FilterState::Lost: return "lost";
    }
    return "unknown";
}

}  // namespace

ServerProvider::ServerProvider() = default;
ServerProvider::~ServerProvider() = default;

vr::EVRInitError ServerProvider::Init(vr::IVRDriverContext* context) {
    VR_INIT_SERVER_DRIVER_CONTEXT(context);
    loadSettings();
    devices_.reset();
    for (auto& slot : slots_) {
        // Most SteamVR device slots are empty or are not Vive Trackers. Construct
        // filter state lazily after a usable tracker pose reaches filterPose().
        slot.filter.reset();
        slot.hasTransform = false;
        slot.transform = {};
        slot.hasDriverFromHead = false;
        slot.driverFromHead = {};
        slot.lastState = FilterState::Cold;
        slot.driverFromHeadWarningLogged = false;
        slot.identifiedLogged = false;
        slot.hasDriverPose = false;
        slot.lastDriverPose = {};
        ++slot.generation;
        resetTransitionLogRate(slot);
    }

    active_.store(true, std::memory_order_release);
    bool hookInstalled = false;
    if (filterEnabled_) {
        hooks_ = std::make_unique<InterfaceHooks>();
        hookInstalled = hooks_->install(this, context, vr::VRServerDriverHost());
    }
    log("PoseAnchor " POSE_ANCHOR_VERSION " loaded (Vive Tracker only, normal poses pass through)");
    if (filterEnabled_ && !hookInstalled) {
        log("warning: no SteamVR pose hook was installed; filtering is inactive");
    }
    if (!filterEnabled_) log("filterEnabled is false; no unsupported pose hook was installed");
    return vr::VRInitError_None;
}

void ServerProvider::Cleanup() {
    active_.store(false, std::memory_order_release);
    if (hooks_) hooks_->remove();
    hooks_.reset();
    for (auto& slot : slots_) {
        std::scoped_lock lock(slot.mutex);
        slot.filter.reset();
    }
    log("PoseAnchor unloaded");
    VR_CLEANUP_SERVER_DRIVER_CONTEXT();
}

const char* const* ServerProvider::GetInterfaceVersions() { return vr::k_InterfaceVersions; }

void ServerProvider::RunFrame() {
    try {
        if (!active_.load(std::memory_order_acquire)) return;
        devices_.classifyPending();
        if (!syntheticWatchdogEnabled_ || !hooks_) return;

        const std::int64_t nowNs = steadyNowNs();
        for (std::uint32_t deviceIndex = 0; deviceIndex < slots_.size(); ++deviceIndex) {
            if (devices_.kind(deviceIndex) != DeviceKind::ViveTracker) continue;
            DeviceSlot& slot = slots_[deviceIndex];
            // Submitting into vrserver has latency outside this driver's control, and
            // the 200+ Hz pose callback contends on slot.mutex. Build the synthetic
            // pose under the lock, submit unlocked, then commit the Lost one-shot only
            // if no real pose or reset re-armed the slot in between.
            vr::DriverPose_t synthetic{};
            std::uint64_t generation = 0;
            bool lost = false;
            {
                std::scoped_lock lock(slot.mutex);
                if (!slot.filter || !slot.hasDriverPose || !slot.hasTransform ||
                    !slot.hasDriverFromHead) continue;
                const auto output = slot.filter->tick(nowNs);
                if (!output) continue;

                synthetic = slot.lastDriverPose;
                synthetic.poseTimeOffset = 0.0;
                PoseAdapter::applyOutput(synthetic, *output, slot.transform);
                if (output->state != slot.lastState) {
                    // tick() has no current pose comparison. Do not attach the previous
                    // push's diagnostics to a callback-silence transition.
                    recordStateChange(deviceIndex, slot, *output, nullptr, nowNs);
                }
                generation = slot.generation;
                lost = output->state == FilterState::Lost;
            }
            const bool submitted = hooks_->submitPose(deviceIndex, synthetic);
            if (submitted && lost) {
                // Publish the terminal OutOfRange pose once, then wait for a real callback.
                // Continuing forever can keep a powered-off tracker alive as a ghost device.
                std::scoped_lock lock(slot.mutex);
                if (slot.generation == generation) slot.hasDriverPose = false;
            }
        }
    } catch (...) {
        log("warning: RunFrame watchdog failed open for this frame");
    }
}

bool ServerProvider::ShouldBlockStandbyMode() { return false; }
void ServerProvider::EnterStandby() {}
void ServerProvider::LeaveStandby() {}

void ServerProvider::filterPose(std::uint32_t deviceIndex, vr::DriverPose_t& pose) {
    if (!active_.load(std::memory_order_acquire) || !filterEnabled_ ||
        deviceIndex >= slots_.size()) {
        return;
    }

    const DeviceKind kind = devices_.kind(deviceIndex);
    if (kind == DeviceKind::Unknown) {
        devices_.requestClassification(deviceIndex);
        return;
    }
    if (kind != DeviceKind::ViveTracker) return;

    DeviceSlot& slot = slots_[deviceIndex];
    std::scoped_lock lock(slot.mutex);

    // A real disconnect always disarms synthetic output before any transform checks.
    if (!pose.deviceIsConnected) {
        if (slot.filter) slot.filter->reset();
        ++slot.generation;
        slot.hasDriverPose = false;
        slot.hasTransform = false;
        slot.hasDriverFromHead = false;
        slot.lastState = FilterState::Cold;
        resetTransitionLogRate(slot);
        return;
    }
    DriverFromHead incomingDriverFromHead{};
    if (!PoseAdapter::readDriverFromHead(pose, incomingDriverFromHead)) {
        if (!slot.driverFromHeadWarningLogged) {
            log("tracker " + devices_.metadata(deviceIndex).serial +
                " has an invalid driver-from-head transform; bypassing for safety");
            slot.driverFromHeadWarningLogged = true;
        }
        if (slot.filter) slot.filter->reset();
        ++slot.generation;
        slot.hasDriverPose = false;
        slot.hasTransform = false;
        slot.hasDriverFromHead = false;
        slot.lastState = FilterState::Cold;
        resetTransitionLogRate(slot);
        return;
    }
    if (slot.hasDriverFromHead &&
        PoseAdapter::transformChanged(slot.driverFromHead, incomingDriverFromHead)) {
        if (slot.filter) slot.filter->reset();
        ++slot.generation;
        slot.lastState = FilterState::Cold;
        slot.driverFromHead = incomingDriverFromHead;
        slot.hasDriverFromHead = true;
        slot.hasDriverPose = false;
        resetTransitionLogRate(slot);
        log("tracker " + devices_.metadata(deviceIndex).serial +
            " device-origin transform changed; filter re-armed without suppressing it");
        return;
    }
    slot.driverFromHead = incomingDriverFromHead;
    slot.hasDriverFromHead = true;
    if (!slot.identifiedLogged) {
        const auto& metadata = devices_.metadata(deviceIndex);
        std::ostringstream stream;
        stream << "identified Vive Tracker " << metadata.serial << " (" << metadata.model
               << ") driverFromHead q=(" << incomingDriverFromHead.rotation.w << ","
               << incomingDriverFromHead.rotation.x << ","
               << incomingDriverFromHead.rotation.y << ","
               << incomingDriverFromHead.rotation.z << ") t=("
               << incomingDriverFromHead.translation.x << ","
               << incomingDriverFromHead.translation.y << ","
               << incomingDriverFromHead.translation.z << ")";
        log(stream.str());
        slot.identifiedLogged = true;
    }

    WorldFromDriver incomingTransform{};
    const bool transformValid = PoseAdapter::readWorldFromDriver(pose, incomingTransform);
    if (transformValid) {
        if (slot.hasTransform && PoseAdapter::transformChanged(slot.transform, incomingTransform)) {
            if (slot.filter) slot.filter->reset();
            ++slot.generation;
            slot.lastState = FilterState::Cold;
            slot.transform = incomingTransform;
            slot.hasDriverPose = false;
            resetTransitionLogRate(slot);
            log("tracker " + devices_.metadata(deviceIndex).serial +
                " tracking-space transform changed; filter re-armed without suppressing it");
            return;
        }
        slot.transform = incomingTransform;
        slot.hasTransform = true;
    } else if (!slot.hasTransform) {
        if (slot.filter) slot.filter->reset();
        ++slot.generation;
        slot.hasDriverPose = false;
        return;
    }

    if (!slot.filter) slot.filter = std::make_unique<PoseFilter>(config_);
    const std::int64_t receiveTimeNs = steadyNowNs();
    PoseSample sample = PoseAdapter::toSample(pose, slot.transform, receiveTimeNs);
    if (!transformValid) {
        // Reuse the last known transform only for a bounded Hold; never accept a
        // Running_OK sample whose world transform is malformed.
        sample.status = InputStatus::Invalid;
    }
    slot.lastDriverPose = pose;
    slot.hasDriverPose = true;
    ++slot.generation;
    const FilterOutput output = slot.filter->push(sample);
    if (output.modified) PoseAdapter::applyOutput(pose, output, slot.transform);

    if (output.state != slot.lastState) {
        const FilterDecisionMetrics metrics = slot.filter->lastDecisionMetrics();
        recordStateChange(deviceIndex, slot, output,
                          metrics.valid ? &metrics : nullptr, receiveTimeNs);
    }
}

void ServerProvider::log(const std::string& message) const noexcept {
    try {
        if (auto* driverLog = vr::VRDriverLog()) {
            const std::string line = "[PoseAnchor] " + message;
            driverLog->Log(line.c_str());
        }
    } catch (...) {
    }
}

void ServerProvider::loadSettings() {
    auto* settings = vr::VRSettings();
    if (!settings) return;

    constexpr double kRadiansPerDegree = 0.017453292519943295;

    const auto getBool = [settings](const char* key, bool fallback) {
        vr::EVRSettingsError error = vr::VRSettingsError_None;
        const bool value = settings->GetBool(kSettingsSection, key, &error);
        return error == vr::VRSettingsError_None ? value : fallback;
    };
    const auto getInt = [settings](const char* key, int fallback) {
        vr::EVRSettingsError error = vr::VRSettingsError_None;
        const int value = settings->GetInt32(kSettingsSection, key, &error);
        return error == vr::VRSettingsError_None ? value : fallback;
    };
    const auto getFloat = [settings](const char* key, double fallback) {
        vr::EVRSettingsError error = vr::VRSettingsError_None;
        const float value = settings->GetFloat(kSettingsSection, key, &error);
        return error == vr::VRSettingsError_None && std::isfinite(value)
            ? static_cast<double>(value) : fallback;
    };

    filterEnabled_ = getBool("filterEnabled", filterEnabled_);
    syntheticWatchdogEnabled_ = getBool(
        "syntheticWatchdogEnabled", syntheticWatchdogEnabled_);
    config_.warmupGoodSamples = getInt("warmupGoodSamples", config_.warmupGoodSamples);
    config_.basePositionGateMeters = getFloat(
        "basePositionGateMeters", config_.basePositionGateMeters);
    // Degree-based keys fall back to the radian defaults in FilterConfig so the
    // two representations cannot drift apart.
    config_.baseRotationGateRadians = getFloat(
        "baseRotationGateDegrees",
        config_.baseRotationGateRadians / kRadiansPerDegree) * kRadiansPerDegree;
    config_.linearAccelerationGate = getFloat(
        "linearAccelerationGate", config_.linearAccelerationGate);
    config_.angularAccelerationGate = getFloat(
        "angularAccelerationGate", config_.angularAccelerationGate);
    config_.velocityConsistencyPositionMeters = getFloat(
        "velocityConsistencyPositionMeters", config_.velocityConsistencyPositionMeters);
    config_.velocityConsistencyRotationRadians = getFloat(
        "velocityConsistencyRotationDegrees",
        config_.velocityConsistencyRotationRadians / kRadiansPerDegree) * kRadiansPerDegree;
    config_.hardMaxLinearSpeed = getFloat(
        "hardMaxLinearSpeed", config_.hardMaxLinearSpeed);
    config_.hardMaxAngularSpeed = getFloat(
        "hardMaxAngularSpeed", config_.hardMaxAngularSpeed);
    config_.holdSeconds = getFloat("holdSeconds", config_.holdSeconds);
    config_.holdDampingSeconds = getFloat(
        "holdDampingSeconds", config_.holdDampingSeconds);
    config_.maxHoldTranslationMeters = getFloat(
        "maxHoldTranslationMeters", config_.maxHoldTranslationMeters);
    config_.maxHoldRotationRadians = getFloat(
        "maxHoldRotationDegrees",
        config_.maxHoldRotationRadians / kRadiansPerDegree) * kRadiansPerDegree;
    config_.candidateConfirmSamples = getInt(
        "candidateConfirmSamples", config_.candidateConfirmSamples);
    config_.recoveryMinSeconds = getFloat(
        "recoveryMinSeconds", config_.recoveryMinSeconds);
    config_.recoveryMaxSeconds = getFloat(
        "recoveryMaxSeconds", config_.recoveryMaxSeconds);
}

void ServerProvider::recordStateChange(std::uint32_t deviceIndex, DeviceSlot& slot,
                                       const FilterOutput& output,
                                       const FilterDecisionMetrics* metrics,
                                       std::int64_t nowNs) const noexcept {
    if (output.state == slot.lastState) return;

    const FilterState from = slot.lastState;
    slot.lastState = output.state;

    const std::size_t index = stateIndex(output.state);
    const std::int64_t lastLogNs = slot.lastTransitionLogNs[index];
    if (lastLogNs != 0 && nowNs >= lastLogNs &&
        nowNs - lastLogNs < kTransitionLogIntervalNs) {
        std::uint32_t& suppressed = slot.suppressedTransitionLogs[index];
        if (suppressed != std::numeric_limits<std::uint32_t>::max()) ++suppressed;
        return;
    }

    const std::uint32_t suppressed = slot.suppressedTransitionLogs[index];
    slot.suppressedTransitionLogs[index] = 0;
    slot.lastTransitionLogNs[index] = nowNs;
    logStateChange(deviceIndex, from, output, metrics, suppressed);
}

void ServerProvider::logStateChange(std::uint32_t deviceIndex, FilterState from,
                                    const FilterOutput& output,
                                    const FilterDecisionMetrics* metrics,
                                    std::uint32_t suppressedLogs) const noexcept {
    try {
        std::ostringstream stream;
        stream << "tracker " << devices_.metadata(deviceIndex).serial << " "
               << stateName(from) << " -> " << stateName(output.state)
               << " reasons=0x" << std::hex << output.reasons;
        if (metrics && metrics->valid) {
            stream << std::dec << std::fixed << std::setprecision(4)
                   << " dt_ms=" << metrics->rawDtSeconds * 1000.0
                   << " used_dt_ms=" << metrics->dtSeconds * 1000.0
                   << " gate_scale=" << metrics->gateScale
                   << " step_m=" << metrics->translationMeters
                   << " step_rad=" << metrics->rotationRadians
                   << " pos_innov_m=" << metrics->positionInnovationMeters << "/"
                   << metrics->positionGateMeters
                   << " rot_innov_rad=" << metrics->rotationInnovationRadians << "/"
                   << metrics->rotationGateRadians
                   << " lin_mps=" << metrics->linearSpeedMetersPerSecond << "/"
                   << metrics->hardLinearSpeedGateMetersPerSecond
                   << " ang_rps=" << metrics->angularSpeedRadiansPerSecond << "/"
                   << metrics->hardAngularSpeedGateRadiansPerSecond
                   << " dlin_mps=" << metrics->linearVelocityDeltaMetersPerSecond << "/"
                   << metrics->linearVelocityDeltaGateMetersPerSecond
                   << " dang_rps=" << metrics->angularVelocityDeltaRadiansPerSecond << "/"
                   << metrics->angularVelocityDeltaGateRadiansPerSecond
                   << " lin_res_m=" << metrics->linearPoseResidualMeters << "/"
                   << metrics->linearPoseResidualGateMeters
                   << " ang_res_rad=" << metrics->angularPoseResidualRadians << "/"
                   << metrics->angularPoseResidualGateRadians
                   << " lin_out_mps=" << metrics->repairedLinearSpeedMetersPerSecond
                   << " ang_out_rps=" << metrics->repairedAngularSpeedRadiansPerSecond
                   << " repair_lin=" << (metrics->linearVelocityRepaired ? 1 : 0)
                   << " repair_ang=" << (metrics->angularVelocityRepaired ? 1 : 0)
                   << " exceeded=0x" << std::hex << metrics->exceededGates
                   << " rejected=0x" << metrics->rejectedGates;
        }
        if (suppressedLogs != 0) {
            stream << std::dec << " suppressed=" << suppressedLogs;
        }
        log(stream.str());
    } catch (...) {
        // Diagnostics must never affect pose delivery, including under allocation failure.
    }
}

void ServerProvider::resetTransitionLogRate(DeviceSlot& slot) noexcept {
    slot.lastTransitionLogNs.fill(0);
    slot.suppressedTransitionLogs.fill(0);
}

std::size_t ServerProvider::stateIndex(FilterState state) noexcept {
    switch (state) {
        case FilterState::Cold: return 0;
        case FilterState::Tracking: return 1;
        case FilterState::Hold: return 2;
        case FilterState::Recovering: return 3;
        case FilterState::Lost: return 4;
    }
    return 0;
}

std::int64_t ServerProvider::steadyNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace pose_anchor::driver
