#include "hooks.hpp"
#include "server_provider.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

// Exercise the real in-process hooks with a fake host and provider. These tests
// neither initialize OpenVR nor contact a running SteamVR instance.
using namespace pose_anchor::driver;
using namespace std::chrono_literals;

namespace {

int failures = 0;

void check(bool condition, std::string_view expression, int line) {
    if (!condition) {
        std::cerr << "line " << line << ": CHECK failed: " << expression << '\n';
        ++failures;
    }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

void testTransformDiagnosticRateLimit() {
    DiagnosticRateLimiter world;
    DiagnosticRateLimiter head;
    CHECK(world.shouldLog(0));
    CHECK(!world.shouldLog(0));
    CHECK(!world.shouldLog(999'999'999));
    CHECK(world.shouldLog(1'000'000'000));
    CHECK(!world.shouldLog(1'000'000'001));
    // One diagnostic category cannot hide the first message of another.
    CHECK(head.shouldLog(1'000'000'001));

    world = {};
    unsigned emitted = 0;
    for (std::int64_t sample = 0; sample < 24'000; ++sample) {
        emitted += world.shouldLog(sample * 5'000'000) ? 1u : 0u;
    }
    // Two minutes of 200 Hz transform changes produce at most one message/s.
    CHECK(emitted == 120);

    // A new session resets the limiter; a backwards clock never suppresses
    // diagnostics indefinitely. Very large signed spans cannot overflow.
    world = {};
    CHECK(world.shouldLog(1'000'000'000));
    CHECK(world.shouldLog(100));
    CHECK(!world.shouldLog(101));
    CHECK(world.shouldLog(std::numeric_limits<std::int64_t>::min()));
    CHECK(world.shouldLog(std::numeric_limits<std::int64_t>::max()));
}

struct Submission {
    int implementation;
    double position;
    bool connected;
};

struct TestState {
    std::mutex mutex;
    std::condition_variable condition;
    std::uint64_t generation{};
    bool armed{};
    bool blockFilter{};
    bool filterEntered{};
    bool releaseFilter{};
    bool throwFilter{};
    bool reenterHost{};
    unsigned filterCalls{};
    std::vector<Submission> submissions;
};

TestState* state = nullptr;

using PoseFn = void (VR_CALLTYPE*)(vr::IVRServerDriverHost*, std::uint32_t,
                                  const vr::DriverPose_t&, std::uint32_t);
using GetInterfaceFn = void* (VR_CALLTYPE*)(vr::IVRDriverContext*, const char*,
                                          vr::EVRInitError*);

struct MockHost {
    void** vtable;
};

struct MockContext {
    void** vtable;
    MockHost* selectedHost;
};

vr::DriverPose_t pose(double position, bool connected = true) {
    vr::DriverPose_t result{};
    result.vecPosition[0] = position;
    result.deviceIsConnected = connected;
    result.poseIsValid = connected;
    result.result = vr::TrackingResult_Running_OK;
    return result;
}

void send(MockHost& host, const vr::DriverPose_t& value, std::uint32_t index = 0) {
    const auto callback = reinterpret_cast<PoseFn>(host.vtable[1]);
    callback(reinterpret_cast<vr::IVRServerDriverHost*>(&host), index, value, sizeof(value));
}

void record(vr::IVRServerDriverHost* host, const vr::DriverPose_t& value,
            int implementation) {
    bool reenter = false;
    {
        std::scoped_lock lock(state->mutex);
        state->submissions.push_back({implementation, value.vecPosition[0],
                                     value.deviceIsConnected});
        reenter = state->reenterHost;
        state->reenterHost = false;
    }
    if (reenter) {
        // The original host is allowed to synchronously trigger another callback.
        // This hangs if a non-recursive route lock is held across the host call.
        send(*reinterpret_cast<MockHost*>(host), pose(value.vecPosition[0] + 1.0));
    }
}

__declspec(noinline) void VR_CALLTYPE hostA(vr::IVRServerDriverHost* host,
                                           std::uint32_t,
                                           const vr::DriverPose_t& value, std::uint32_t) {
    record(host, value, 1);
}

__declspec(noinline) void VR_CALLTYPE hostB(vr::IVRServerDriverHost* host,
                                           std::uint32_t,
                                           const vr::DriverPose_t& value, std::uint32_t) {
    record(host, value, 2);
}

__declspec(noinline) void* VR_CALLTYPE getInterface(vr::IVRDriverContext* context,
                                                   const char*, vr::EVRInitError* error) {
    if (error) *error = vr::VRInitError_None;
    return reinterpret_cast<MockContext*>(context)->selectedHost;
}

void testStaleWatchdogAndDisconnect() {
    TestState testState;
    state = &testState;
    ServerProvider provider;
    InterfaceHooks hooks;
    void* hostTable[]{nullptr, reinterpret_cast<void*>(&hostA)};
    MockHost host{hostTable};
    CHECK(hooks.install(&provider, nullptr, reinterpret_cast<vr::IVRServerDriverHost*>(&host)));
    send(host, pose(1.0));
    const std::uint64_t snapshot = testState.generation;
    send(host, pose(2.0));
    CHECK(!hooks.submitPose(0, pose(999.0), snapshot));
    CHECK(testState.submissions.size() == 2);
    CHECK(hooks.submitPose(0, pose(3.0), testState.generation));
    CHECK(testState.submissions.size() == 3);
    const std::uint64_t beforeDisconnect = testState.generation;
    send(host, pose(4.0, false));
    CHECK(!hooks.submitPose(0, pose(999.0), beforeDisconnect));
    CHECK(!hooks.submitPose(0, pose(999.0), testState.generation));
    CHECK(testState.submissions.size() == 4);
    CHECK(!testState.submissions.back().connected);
    hooks.remove();
    CHECK(!hooks.submitPose(0, pose(999.0), testState.generation));
}

void testWatchdogRacingNewPose() {
    TestState testState;
    state = &testState;
    ServerProvider provider;
    InterfaceHooks hooks;
    void* hostTable[]{nullptr, reinterpret_cast<void*>(&hostA)};
    MockHost host{hostTable};
    CHECK(hooks.install(&provider, nullptr, reinterpret_cast<vr::IVRServerDriverHost*>(&host)));
    send(host, pose(1.0));
    const std::uint64_t snapshot = testState.generation;
    testState.blockFilter = true;

    std::thread realPose([&] { send(host, pose(2.0)); });
    {
        std::unique_lock lock(testState.mutex);
        CHECK(testState.condition.wait_for(lock, 5s, [&] { return testState.filterEntered; }));
    }
    std::atomic_bool watchdogStarted{false};
    bool submitted = true;
    std::thread watchdog([&] {
        watchdogStarted.store(true, std::memory_order_release);
        submitted = hooks.submitPose(0, pose(999.0), snapshot);
    });
    while (!watchdogStarted.load(std::memory_order_acquire)) std::this_thread::yield();
    {
        std::scoped_lock lock(testState.mutex);
        testState.releaseFilter = true;
    }
    testState.condition.notify_all();
    realPose.join();
    watchdog.join();
    CHECK(!submitted);
    CHECK(testState.submissions.size() == 2);
    CHECK(testState.submissions.back().position == 2.0);
    hooks.remove();
}

void testReentryAndFailOpen() {
    TestState testState;
    state = &testState;
    ServerProvider provider;
    InterfaceHooks hooks;
    void* hostTable[]{nullptr, reinterpret_cast<void*>(&hostA)};
    MockHost host{hostTable};
    CHECK(hooks.install(&provider, nullptr, reinterpret_cast<vr::IVRServerDriverHost*>(&host)));
    testState.reenterHost = true;
    send(host, pose(1.0));
    CHECK(testState.submissions.size() == 2);
    CHECK(testState.submissions.back().position == 2.0);

    testState.throwFilter = true;
    const std::uint64_t beforeFailure = testState.generation;
    send(host, pose(3.0));
    CHECK(testState.submissions.size() == 3);
    CHECK(testState.submissions.back().position == 3.0);
    CHECK(!hooks.submitPose(0, pose(999.0), beforeFailure));
    const unsigned beforeNonTarget = testState.filterCalls;
    send(host, pose(4.0), 8);
    CHECK(testState.submissions.size() == 4);
    CHECK(testState.filterCalls == beforeNonTarget);
    hooks.remove();
    send(host, pose(5.0));
    CHECK(testState.submissions.size() == 5);
    CHECK(testState.filterCalls == beforeNonTarget);
}

void testSecondHostDoesNotReplaceLiveTrampoline() {
    TestState testState;
    state = &testState;
    ServerProvider provider;
    InterfaceHooks hooks;
    void* tableA[]{nullptr, reinterpret_cast<void*>(&hostA)};
    void* tableB[]{nullptr, reinterpret_cast<void*>(&hostB)};
    MockHost first{tableA};
    MockHost second{tableB};
    void* contextTable[]{reinterpret_cast<void*>(&getInterface)};
    MockContext context{contextTable, &second};
    CHECK(hooks.install(&provider, reinterpret_cast<vr::IVRDriverContext*>(&context),
                        reinterpret_cast<vr::IVRServerDriverHost*>(&first)));
    const auto discover = reinterpret_cast<GetInterfaceFn>(context.vtable[0]);
    CHECK(discover(reinterpret_cast<vr::IVRDriverContext*>(&context),
                   "IVRServerDriverHost_006", nullptr) == &second);
    send(first, pose(1.0));
    send(second, pose(2.0));
    CHECK(testState.submissions.size() == 2);
    CHECK(testState.submissions[0].implementation == 1);
    CHECK(testState.submissions[1].implementation == 2);
    CHECK(testState.filterCalls == 1);
    hooks.remove();
    send(first, pose(3.0));
    send(second, pose(4.0));
    CHECK(testState.submissions.size() == 4);
    CHECK(testState.filterCalls == 1);
}

}  // namespace

namespace pose_anchor::driver {

// The provider test double models the real slot generation/armed contract, while
// hooks.cpp, MinHook, forwarding and concurrent/reentrant execution remain real.
ServerProvider::ServerProvider() = default;
ServerProvider::~ServerProvider() = default;
vr::EVRInitError ServerProvider::Init(vr::IVRDriverContext*) { return vr::VRInitError_None; }
void ServerProvider::Cleanup() {}
const char* const* ServerProvider::GetInterfaceVersions() { return vr::k_InterfaceVersions; }
void ServerProvider::RunFrame() {}
bool ServerProvider::ShouldBlockStandbyMode() { return false; }
void ServerProvider::EnterStandby() {}
void ServerProvider::LeaveStandby() {}
void ServerProvider::log(const std::string&) const noexcept {}
bool ServerProvider::isFilterTarget(std::uint32_t index) noexcept { return index == 0; }

bool ServerProvider::filterPose(std::uint32_t, const vr::DriverPose_t& raw,
                                 vr::DriverPose_t& filtered) {
    std::unique_lock lock(state->mutex);
    ++state->filterCalls;
    ++state->generation;
    state->armed = false;
    if (state->throwFilter) throw std::runtime_error("filter failure");
    state->armed = raw.deviceIsConnected;
    filtered = raw;
    if (state->blockFilter) {
        state->filterEntered = true;
        state->condition.notify_all();
        state->condition.wait(lock, [] { return state->releaseFilter; });
    }
    return true;
}

bool ServerProvider::isCurrentPoseGeneration(std::uint32_t,
                                             std::uint64_t generation) noexcept {
    std::scoped_lock lock(state->mutex);
    return state->armed && state->generation == generation;
}

}  // namespace pose_anchor::driver

int main() {
    testTransformDiagnosticRateLimit();
    testStaleWatchdogAndDisconnect();
    testWatchdogRacingNewPose();
    testReentryAndFailOpen();
    testSecondHostDoesNotReplaceLiveTrampoline();
    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All PoseAnchor hook tests passed\n";
    return EXIT_SUCCESS;
}
