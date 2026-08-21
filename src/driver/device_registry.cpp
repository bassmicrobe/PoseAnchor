#include "device_registry.hpp"

#include <algorithm>
#include <cctype>

namespace pose_anchor::driver {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool contains(const std::string& value, const char* needle) {
    return value.find(needle) != std::string::npos;
}

bool shouldRetry(vr::ETrackedPropertyError error) {
    return error == vr::TrackedProp_NotYetAvailable || error == vr::TrackedProp_UnknownProperty;
}

std::string propertyString(vr::CVRPropertyHelpers* properties,
                           vr::PropertyContainerHandle_t container,
                           vr::ETrackedDeviceProperty property,
                           bool& retry) {
    vr::ETrackedPropertyError error = vr::TrackedProp_Success;
    std::string value = properties->GetStringProperty(container, property, &error);
    retry = retry || shouldRetry(error);
    return error == vr::TrackedProp_Success ? value : std::string{};
}

}  // namespace

DeviceKind classifyIdentity(const DeviceIdentity& identity) {
    if (!identity.isGenericTracker) return DeviceKind::Other;

    const std::string registered = lower(identity.registeredType);
    const std::string model = lower(identity.model);
    const std::string system = lower(identity.actualTrackingSystem.empty()
                                         ? identity.trackingSystem
                                         : identity.actualTrackingSystem);
    const std::string controller = lower(identity.controllerType);
    const std::string maker = lower(identity.manufacturer);

    const bool registeredTracker = contains(registered, "htc/vive_tracker") ||
                                   contains(registered, "vive_tracker");
    const bool lighthouse = contains(system, "lighthouse");
    const bool controllerTracker = contains(controller, "vive_tracker");
    const bool modelTracker = contains(model, "vive tracker") && contains(maker, "htc");
    if (lighthouse && (registeredTracker || controllerTracker || modelTracker)) {
        return DeviceKind::ViveTracker;
    }
    return DeviceKind::Other;
}

DeviceRegistry::DeviceRegistry() {
    reset();
}

void DeviceRegistry::reset() {
    for (auto& value : kinds_) value.store(DeviceKind::Unknown, std::memory_order_relaxed);
    for (auto& value : pending_) value.store(false, std::memory_order_relaxed);
    for (auto& value : metadata_) value = {};
}

DeviceKind DeviceRegistry::kind(std::uint32_t index) const noexcept {
    if (index >= kinds_.size()) return DeviceKind::Other;
    return kinds_[index].load(std::memory_order_acquire);
}

void DeviceRegistry::requestClassification(std::uint32_t index) noexcept {
    if (index < pending_.size()) pending_[index].store(true, std::memory_order_release);
}

void DeviceRegistry::classifyPending() {
    for (std::uint32_t index = 0; index < pending_.size(); ++index) {
        if (!pending_[index].exchange(false, std::memory_order_acq_rel)) continue;
        if (kinds_[index].load(std::memory_order_acquire) != DeviceKind::Unknown) continue;

        DeviceMetadata metadata{};
        bool retry = false;
        const DeviceKind result = classify(index, metadata, retry);
        if (result != DeviceKind::Unknown) {
            metadata_[index] = std::move(metadata);
            kinds_[index].store(result, std::memory_order_release);
        } else if (retry) {
            pending_[index].store(true, std::memory_order_release);
        }
    }
}

const DeviceMetadata& DeviceRegistry::metadata(std::uint32_t index) const noexcept {
    static const DeviceMetadata empty{};
    return index < metadata_.size() ? metadata_[index] : empty;
}

DeviceKind DeviceRegistry::classify(std::uint32_t index, DeviceMetadata& metadata,
                                    bool& retry) const {
    auto* properties = vr::VRProperties();
    if (!properties) {
        retry = true;
        return DeviceKind::Unknown;
    }
    const auto container = properties->TrackedDeviceToPropertyContainer(index);

    vr::ETrackedPropertyError classError = vr::TrackedProp_Success;
    const auto deviceClass = static_cast<vr::ETrackedDeviceClass>(
        properties->GetInt32Property(container, vr::Prop_DeviceClass_Int32, &classError));
    if (classError != vr::TrackedProp_Success) {
        retry = shouldRetry(classError);
        return DeviceKind::Unknown;
    }
    if (deviceClass != vr::TrackedDeviceClass_GenericTracker) return DeviceKind::Other;

    DeviceIdentity identity{};
    identity.isGenericTracker = true;
    metadata.serial = propertyString(properties, container, vr::Prop_SerialNumber_String, retry);
    metadata.model = propertyString(properties, container, vr::Prop_ModelNumber_String, retry);
    metadata.registeredType = propertyString(
        properties, container, vr::Prop_RegisteredDeviceType_String, retry);
    identity.model = metadata.model;
    identity.registeredType = metadata.registeredType;
    identity.trackingSystem = propertyString(
        properties, container, vr::Prop_TrackingSystemName_String, retry);
    identity.actualTrackingSystem = propertyString(
        properties, container, vr::Prop_ActualTrackingSystemName_String, retry);
    identity.controllerType = propertyString(
        properties, container, vr::Prop_ControllerType_String, retry);
    identity.manufacturer = propertyString(
        properties, container, vr::Prop_ManufacturerName_String, retry);

    const DeviceKind result = classifyIdentity(identity);
    // A generic tracker whose identity properties are still arriving must stay
    // fail-open; a positive match wins even with fetches pending.
    if (result == DeviceKind::Other && retry) return DeviceKind::Unknown;
    return result;
}

}  // namespace pose_anchor::driver
