#include "status_overview.hpp"
#include <iostream>

using namespace pose_anchor::status;

int main() {
    int failures = 0;
    const auto check = [&](bool result, const char* message) {
        if (!result) { ++failures; std::cerr << message << '\n'; }
    };
    const auto registered = RegistrationState::Registered;
    const auto yes = EvidenceState::Yes;
    check(selectOverview(registered, true, yes, yes, false, 0) == OverviewState::WaitingForTracker,
          "Loaded hook without recognition must not claim a recognized tracker");
    check(selectOverview(registered, true, yes, yes, false, 3) == OverviewState::Ready,
          "Verified driver and recognized history are ready");
    check(selectOverview(registered, false, yes, yes, false, 3) == OverviewState::SteamVrStopped,
          "Stopped process must override earlier evidence");
    for (const bool running : {false, true}) {
        check(selectOverview(registered, running, yes, yes, false, 3, false) == OverviewState::SteamVrUnknown,
              "Incomplete process inspection must claim neither stopped nor ready");
    }
    check(selectOverview(registered, true, yes, yes, true, 3) == OverviewState::Disabled,
          "Disabled filtering must override successful hook evidence");
    for (auto unknown : {EvidenceState::Unknown, EvidenceState::No, EvidenceState::NotApplicable}) {
        check(selectOverview(registered, true, unknown, yes, false, 3) == OverviewState::DriverUnavailable,
              "Unverified driver must not appear ready");
        check(selectOverview(registered, true, yes, unknown, false, 3) == OverviewState::HookUnavailable,
              "Unverified hook must not appear ready");
    }
    const struct { RegistrationState registration; OverviewState expected; } cases[] = {
        {RegistrationState::NotRegistered, OverviewState::NotRegistered},
        {RegistrationState::RegisteredElsewhere, OverviewState::RegisteredElsewhere},
        {RegistrationState::ConflictingRegistrations, OverviewState::ConflictingRegistrations},
        {RegistrationState::InspectionIncomplete, OverviewState::InspectionIncomplete},
        {RegistrationState::BrokenInstallation, OverviewState::BrokenInstallation},
        {RegistrationState::Unknown, OverviewState::UnknownRegistration},
    };
    for (const auto& entry : cases) {
        check(selectOverview(entry.registration, true, yes, yes, false, 3) == entry.expected,
              "Registration problem must override runtime evidence");
    }
    if (!failures) std::cout << "Status overview tests passed\n";
    return failures ? 1 : 0;
}
