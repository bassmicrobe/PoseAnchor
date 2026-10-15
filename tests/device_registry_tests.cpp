#include "device_registry.hpp"

#include <cstdlib>
#include <chrono>
#include <cstring>
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

class TestProperties final : public vr::IVRProperties {
public:
    unsigned reads{};
    bool trackerIdentityReady{};

    vr::ETrackedPropertyError ReadPropertyBatch(vr::PropertyContainerHandle_t,
                                                 vr::PropertyRead_t* batch,
                                                 std::uint32_t count) override {
        reads += count;
        for (std::uint32_t i = 0; i < count; ++i) {
            auto& item = batch[i];
            const std::int32_t deviceClass = vr::TrackedDeviceClass_GenericTracker;
            if (item.prop == vr::Prop_DeviceClass_Int32) {
                set(item, &deviceClass, sizeof(deviceClass), vr::k_unInt32PropertyTag);
            } else if (item.prop == vr::Prop_TrackingSystemName_String) {
                const char* system = trackerIdentityReady ? "lighthouse" : "other_vendor";
                set(item, system, std::strlen(system) + 1, vr::k_unStringPropertyTag);
            } else if (item.prop == vr::Prop_RegisteredDeviceType_String) {
                const char* type = trackerIdentityReady ? "htc/vive_tracker" : "other_tracker";
                set(item, type, std::strlen(type) + 1, vr::k_unStringPropertyTag);
            } else {
                item.eError = vr::TrackedProp_UnknownProperty;
                item.unRequiredBufferSize = 0;
                item.unTag = vr::k_unInvalidPropertyTag;
            }
        }
        return vr::TrackedProp_Success;
    }

    vr::ETrackedPropertyError WritePropertyBatch(vr::PropertyContainerHandle_t,
                                                  vr::PropertyWrite_t*,
                                                  std::uint32_t) override {
        return vr::TrackedProp_InvalidOperation;
    }
    const char* GetPropErrorNameFromEnum(vr::ETrackedPropertyError) override { return "test"; }
    vr::PropertyContainerHandle_t TrackedDeviceToPropertyContainer(
        vr::TrackedDeviceIndex_t index) override { return index + 1; }

private:
    static void set(vr::PropertyRead_t& item, const void* value, std::size_t bytes,
                    vr::PropertyTypeTag_t tag) {
        item.unTag = tag;
        item.unRequiredBufferSize = static_cast<std::uint32_t>(bytes);
        item.eError = item.unBufferSize < bytes ? vr::TrackedProp_BufferTooSmall
                                               : vr::TrackedProp_Success;
        if (item.eError == vr::TrackedProp_Success) std::memcpy(item.pvBuffer, value, bytes);
    }
};

class TestContext final : public vr::IVRDriverContext {
public:
    TestProperties properties;

    void* GetGenericInterface(const char* version, vr::EVRInitError* error) override {
        const bool matched = std::strcmp(version, vr::IVRProperties_Version) == 0;
        if (error) *error = matched ? vr::VRInitError_None : vr::VRInitError_Init_InterfaceNotFound;
        return matched ? &properties : nullptr;
    }
    vr::DriverHandle_t GetDriverHandle() override { return 1; }
};

void testOptionalPropertyRetriesAreBounded() {
    using namespace std::chrono_literals;
    TestContext context;
    vr::VRDriverContext() = &context;
    DeviceRegistry registry;
    const auto start = std::chrono::steady_clock::time_point{} + 1s;
    registry.requestClassification(0);
    registry.classifyPendingAt(start);
    CHECK(registry.kind(0) == DeviceKind::Unknown);
    const unsigned firstReads = context.properties.reads;
    CHECK(firstReads >= 8);
    for (int i = 1; i < 100; ++i) {
        registry.requestClassification(0);
        registry.classifyPendingAt(start + std::chrono::milliseconds(i));
    }
    CHECK(context.properties.reads == firstReads);
    registry.classifyPendingAt(start + 100ms);
    CHECK(context.properties.reads == 2 * firstReads);

    // A newly discovered device is classified immediately, even while another
    // slot is backing off. Missing optional properties cannot defeat a match.
    context.properties.trackerIdentityReady = true;
    registry.requestClassification(1);
    registry.classifyPendingAt(start + 101ms);
    CHECK(registry.kind(1) == DeviceKind::ViveTracker);
    CHECK(registry.kind(0) == DeviceKind::Unknown);
    const unsigned readsBeforeRetry = context.properties.reads;
    registry.classifyPendingAt(start + 299ms);
    CHECK(context.properties.reads == readsBeforeRetry);
    registry.classifyPendingAt(start + 300ms);
    CHECK(registry.kind(0) == DeviceKind::ViveTracker);

    // Session reset must discard retry deadlines and classify fresh slots now.
    registry.reset();
    registry.requestClassification(0);
    registry.classifyPendingAt(start);
    CHECK(registry.kind(0) == DeviceKind::ViveTracker);
    vr::CleanupDriverContext();
}

}  // namespace

int main() {
    testOptionalPropertyRetriesAreBounded();
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
