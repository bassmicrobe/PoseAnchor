#pragma once

#include "status_parser.hpp"

#include <cstddef>

namespace pose_anchor::status {

enum class EvidenceState { Yes, No, Unknown, NotApplicable };
enum class OverviewState {
    Ready, WaitingForTracker, SteamVrStopped, SteamVrUnknown, Disabled, NotRegistered,
    RegisteredElsewhere, ConflictingRegistrations, InspectionIncomplete,
    BrokenInstallation, UnknownRegistration, DriverUnavailable, HookUnavailable,
};

// Recognition is session history, not current connectivity or tracking quality.
// Ready means only that registration, driver load and the filtering hook are proven.
[[nodiscard]] inline OverviewState selectOverview(
    RegistrationState registration, bool running, EvidenceState loaded,
    EvidenceState hook, bool filterDisabled, std::size_t recognizedTrackers,
    bool processInspectionComplete = true) noexcept {
    switch (registration) {
        case RegistrationState::NotRegistered: return OverviewState::NotRegistered;
        case RegistrationState::RegisteredElsewhere: return OverviewState::RegisteredElsewhere;
        case RegistrationState::ConflictingRegistrations: return OverviewState::ConflictingRegistrations;
        case RegistrationState::InspectionIncomplete: return OverviewState::InspectionIncomplete;
        case RegistrationState::BrokenInstallation: return OverviewState::BrokenInstallation;
        case RegistrationState::Unknown: return OverviewState::UnknownRegistration;
        case RegistrationState::Registered: break;
    }
    if (!processInspectionComplete) return OverviewState::SteamVrUnknown;
    if (!running) return OverviewState::SteamVrStopped;
    if (loaded != EvidenceState::Yes) return OverviewState::DriverUnavailable;
    if (filterDisabled) return OverviewState::Disabled;
    if (hook != EvidenceState::Yes) return OverviewState::HookUnavailable;
    return recognizedTrackers == 0 ? OverviewState::WaitingForTracker : OverviewState::Ready;
}

} // namespace pose_anchor::status
