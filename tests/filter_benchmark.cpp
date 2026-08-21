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

}  // namespace

int main() {
    PoseFilter filter;
    std::int64_t timeNs = 0;
    for (int i = 0; i < kWarmupSamples; ++i) {
        timeNs += kStepNs;
        const double seconds = static_cast<double>(timeNs) * 1e-9;
        (void)filter.push(makeSample(timeNs, seconds));
    }

    // Pre-generate the full input sequence so the timed region measures only
    // PoseFilter::push. The whole sequence is kept (not a reused ring) because the
    // filter needs monotonic timestamps coherent with the constant-velocity motion.
    std::vector<PoseSample> samples;
    samples.reserve(kMeasuredSamples);
    for (int i = 0; i < kMeasuredSamples; ++i) {
        timeNs += kStepNs;
        samples.push_back(makeSample(timeNs, static_cast<double>(timeNs) * 1e-9));
    }

    std::uint64_t modified = 0;
    const auto started = std::chrono::steady_clock::now();
    for (const PoseSample& input : samples) {
        modified += filter.push(input).modified ? 1u : 0u;
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const double totalNanoseconds = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    const double nanosecondsPerSample = totalNanoseconds / kMeasuredSamples;
    const double samplesPerSecond = 1e9 / nanosecondsPerSample;

    std::cout << std::fixed << std::setprecision(2)
              << "samples=" << kMeasuredSamples << '\n'
              << "ns_per_sample=" << nanosecondsPerSample << '\n'
              << "samples_per_second=" << samplesPerSecond << '\n'
              << "pose_filter_bytes=" << sizeof(PoseFilter) << '\n'
              << "decision_metrics_bytes=" << sizeof(FilterDecisionMetrics) << '\n'
              << "modified_samples=" << modified << '\n';
    return filter.state() == FilterState::Tracking && modified == 0 ? 0 : 1;
}
