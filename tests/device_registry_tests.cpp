#include "device_registry.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

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

DeviceIdentity lighthouseTracker() {
    DeviceIdentity identity{};
    identity.isGenericTracker = true;
    identity.trackingSystem = "lighthouse";
    return identity;
}

}  // namespace

int main() {
    // A real Vive Tracker 3.0 as SteamVR reports it.
    {
        DeviceIdentity identity = lighthouseTracker();
        identity.registeredType = "htc/vive_trackerLHR-12345678";
        identity.model = "VIVE Tracker 3.0 MV";
        identity.manufacturer = "HTC";
        CHECK(classifyIdentity(identity) == DeviceKind::ViveTracker);
    }

    // Each identity signal is sufficient on its own when lighthouse-tracked.
    {
        DeviceIdentity identity = lighthouseTracker();
        identity.registeredType = "htc/vive_tracker";
        CHECK(classifyIdentity(identity) == DeviceKind::ViveTracker);
    }
    {
        DeviceIdentity identity = lighthouseTracker();
        identity.controllerType = "vive_tracker_handed";
        CHECK(classifyIdentity(identity) == DeviceKind::ViveTracker);
    }
    {
        DeviceIdentity identity = lighthouseTracker();
        identity.model = "Vive Tracker Pro";
        identity.manufacturer = "HTC Corporation";
        CHECK(classifyIdentity(identity) == DeviceKind::ViveTracker);
    }

    // Matching is case-insensitive.
    {
        DeviceIdentity identity = lighthouseTracker();
        identity.trackingSystem = "Lighthouse";
        identity.registeredType = "HTC/VIVE_Tracker";
        CHECK(classifyIdentity(identity) == DeviceKind::ViveTracker);
    }

    // ActualTrackingSystemName wins over TrackingSystemName when present, so a
    // vendor shim reporting its own system name still classifies by the real one.
    {
        DeviceIdentity identity = lighthouseTracker();
        identity.trackingSystem = "some_shim";
        identity.actualTrackingSystem = "lighthouse";
        identity.registeredType = "htc/vive_tracker";
        CHECK(classifyIdentity(identity) == DeviceKind::ViveTracker);
    }
    {
        DeviceIdentity identity = lighthouseTracker();
        identity.actualTrackingSystem = "oculus";
        identity.registeredType = "htc/vive_tracker";
        CHECK(classifyIdentity(identity) == DeviceKind::Other);
    }

    // Non-lighthouse generic trackers (SlimeVR, owoTrack, Standable and similar
    // full-body solutions) must pass through untouched.
    {
        DeviceIdentity identity{};
        identity.isGenericTracker = true;
        identity.trackingSystem = "slimevr";
        identity.registeredType = "slimevr/tracker";
        identity.model = "SlimeVR Tracker";
        CHECK(classifyIdentity(identity) == DeviceKind::Other);
    }

    // A vive_tracker-branded identity that is not lighthouse-tracked stays Other.
    {
        DeviceIdentity identity{};
        identity.isGenericTracker = true;
        identity.registeredType = "htc/vive_tracker";
        CHECK(classifyIdentity(identity) == DeviceKind::Other);
    }

    // A lighthouse device without any Vive Tracker identity (e.g. Tundra
    // Tracker registers as tundra_labs/*) stays Other.
    {
        DeviceIdentity identity = lighthouseTracker();
        identity.registeredType = "tundra_labs/tundra_tracker";
        identity.model = "Tundra Tracker";
        identity.manufacturer = "Tundra Labs";
        CHECK(classifyIdentity(identity) == DeviceKind::Other);
    }

    // "vive tracker" in the model only counts together with an HTC manufacturer,
    // so third-party accessories echoing the model name do not match.
    {
        DeviceIdentity identity = lighthouseTracker();
        identity.model = "Vive Tracker compatible dongle";
        identity.manufacturer = "SomeVendor";
        CHECK(classifyIdentity(identity) == DeviceKind::Other);
    }

    // Non-tracker device classes never classify as Vive Tracker.
    {
        DeviceIdentity identity{};
        identity.isGenericTracker = false;
        identity.trackingSystem = "lighthouse";
        identity.registeredType = "htc/vive_tracker";
        CHECK(classifyIdentity(identity) == DeviceKind::Other);
    }

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All PoseAnchor device registry tests passed\n";
    return EXIT_SUCCESS;
}
