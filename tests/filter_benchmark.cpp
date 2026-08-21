#include "pose_anchor/tracker_filter.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace pose_anchor;

namespace {

constexpr std::int64_t kStepNs = 1'000'000;
constexpr int kWarmupSamples = 8;
constexpr int kMeasuredSamples = 1'000'000;

PoseSample makeSample(std::int64_t timeNs, double timeSeconds) {
    constexpr double linearSpeed = 1.5;
    constexpr double angularSpeed = 8.0;
    PoseSample sample{};
    sample.timeNs = timeNs;
    sample.arrivalTimeNs = timeNs;
    sample.status = InputStatus::RunningOk;
    sample.world.position = {linearSpeed * timeSeconds, 0.0, 0.0};
    sample.world.orientation = fromRotationVector({0.0, angularSpeed * timeSeconds, 0.0});
    sample.world.linearVelocity = {linearSpeed, 0.0, 0.0};
    sample.world.angularVelocity = {0.0, angularSpeed, 0.0};
    return sample;
}

struct PhaseResult {
    double nanosecondsPerSample{};
    std::uint64_t modified{};
};

// Timing wraps only PoseFilter::push; the input sequence is pre-generated.
PhaseResult timePushes(PoseFilter& filter, const std::vector<PoseSample>& samples) {
    PhaseResult result{};
    const auto started = std::chrono::steady_clock::now();
    for (const PoseSample& input : samples) {
        result.modified += filter.push(input).modified ? 1u : 0u;
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    result.nanosecondsPerSample = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()) /
        static_cast<double>(samples.size());
    return result;
}

}  // namespace

int main() {
    PoseFilter filter;
    std::int64_t timeNs = 0;
    for (int i = 0; i < kWarmupSamples; ++i) {
        timeNs += kStepNs;
        const double seconds = static_cast<double>(timeNs) * 1e-9;
        (void)filter.push(makeSample(timeNs, seconds));
    }

    // Phase 1: clean Tracking. The whole sequence is kept (not a reused ring)
    // because the filter needs monotonic timestamps coherent with the
    // constant-velocity motion.
    PhaseResult tracking{};
    {
        std::vector<PoseSample> samples;
        samples.reserve(kMeasuredSamples);
        for (int i = 0; i < kMeasuredSamples; ++i) {
            timeNs += kStepNs;
            samples.push_back(makeSample(timeNs, static_cast<double>(timeNs) * 1e-9));
        }
        tracking = timePushes(filter, samples);
    }
    const bool trackingClean = filter.state() == FilterState::Tracking &&
                               tracking.modified == 0;

    // Phase 2: disturbed input exercising the outlier/Hold/Recovery paths, which
    // dominate cost exactly when tracking is already struggling. Per 128-sample
    // block: one single-frame spike (reject + Hold + direct return) and one
    // persistent 5 cm shift (Hold + candidate + quick-return Recovery blending).
    PhaseResult disturbed{};
    {
        std::vector<PoseSample> samples;
        samples.reserve(kMeasuredSamples);
        Vec3 shift{};
        for (int i = 0; i < kMeasuredSamples; ++i) {
            timeNs += kStepNs;
            PoseSample sample = makeSample(timeNs, static_cast<double>(timeNs) * 1e-9);
            const int phase = i % 128;
            if (phase == 64) {
                sample.world.position += {1.5, 0.0, 0.0};
            } else if (phase == 96) {
                shift += {0.0, 0.05, 0.0};
            }
            sample.world.position += shift;
            samples.push_back(sample);
        }
        disturbed = timePushes(filter, samples);
    }
    const FilterDiagnostics diagnostics = filter.diagnostics();
    const bool disturbedExercised = diagnostics.holdEntries > 0 &&
                                    diagnostics.recoveryEntries > 0;

    std::cout << std::fixed << std::setprecision(2)
              << "samples_per_phase=" << kMeasuredSamples << '\n'
              << "tracking_ns_per_sample=" << tracking.nanosecondsPerSample << '\n'
              << "tracking_samples_per_second="
              << 1e9 / tracking.nanosecondsPerSample << '\n'
              << "tracking_modified_samples=" << tracking.modified << '\n'
              << "disturbed_ns_per_sample=" << disturbed.nanosecondsPerSample << '\n'
              << "disturbed_modified_samples=" << disturbed.modified << '\n'
              << "disturbed_hold_entries=" << diagnostics.holdEntries << '\n'
              << "disturbed_recovery_entries=" << diagnostics.recoveryEntries << '\n'
              << "disturbed_rejected_samples=" << diagnostics.rejectedSamples << '\n'
              << "pose_filter_bytes=" << sizeof(PoseFilter) << '\n'
              << "decision_metrics_bytes=" << sizeof(FilterDecisionMetrics) << '\n';
    return trackingClean && disturbedExercised ? 0 : 1;
}
