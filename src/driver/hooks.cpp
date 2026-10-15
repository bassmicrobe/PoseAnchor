#include "hooks.hpp"

#include "server_provider.hpp"

#include <cstring>
#include <thread>
#include <MinHook.h>

#if !defined(_WIN64)
#error PoseAnchor's SteamVR hook is currently supported only on Windows x64.
#endif

namespace pose_anchor::driver {

std::atomic<InterfaceHooks*> InterfaceHooks::instance_{nullptr};
std::atomic_uint32_t InterfaceHooks::inFlight_{0};
std::atomic_bool InterfaceHooks::detourShutdown_{false};
std::atomic<InterfaceHooks::GetInterfaceFn> InterfaceHooks::originalGetInterface_{nullptr};
std::atomic<InterfaceHooks::PoseFn> InterfaceHooks::originalPose005_{nullptr};
std::atomic<InterfaceHooks::PoseFn> InterfaceHooks::originalPose006_{nullptr};

InterfaceHooks::~InterfaceHooks() { remove(); }

bool InterfaceHooks::install(ServerProvider* provider, vr::IVRDriverContext* context,
                             vr::IVRServerDriverHost* currentHost) {
    std::scoped_lock lock(mutex_);
    if (minHookInitialized_) return true;
    shuttingDown_ = false;
    detourShutdown_.store(false, std::memory_order_seq_cst);
    provider_ = provider;
    instance_.store(this, std::memory_order_release);

    const MH_STATUS initializeResult = MH_Initialize();
    if (initializeResult != MH_OK && initializeResult != MH_ERROR_ALREADY_INITIALIZED) {
        if (provider_) provider_->log(std::string("MinHook initialization failed: ") +
                                      MH_StatusToString(initializeResult));
        instance_.store(nullptr, std::memory_order_release);
        provider_ = nullptr;
        return false;
    }
    minHookInitialized_ = true;

    // VR_INIT_SERVER_DRIVER_CONTEXT already requested _006 before our context hook exists.
    const bool poseInstalled = currentHost && installPoseHook(currentHost, true);
    const bool contextInstalled = installContextHook(context);
    if (provider_) {
        provider_->log(poseInstalled
            ? "IVRServerDriverHost_006 pose hook installed"
            : "warning: current pose host could not be hooked; waiting for a later interface request");
    }
    return poseInstalled || contextInstalled;
}

void InterfaceHooks::remove() {
    std::unique_lock lock(mutex_);
    if (!minHookInitialized_) return;

    shuttingDown_ = true;
    instance_.store(nullptr, std::memory_order_release);
    detourShutdown_.store(true, std::memory_order_seq_cst);
    if (poseTarget005_) {
        MH_DisableHook(poseTarget005_);
    }
    if (poseTarget006_ && poseTarget006_ != poseTarget005_) {
        MH_DisableHook(poseTarget006_);
    }
    if (contextTarget_) {
        MH_DisableHook(contextTarget_);
    }

    lock.unlock();
    while (inFlight_.load(std::memory_order_seq_cst) != 0) {
        std::this_thread::yield();
    }
    lock.lock();
    provider_ = nullptr;
    for (auto& route : poseRoutes_) {
        std::scoped_lock routeLock(route.mutex);
        route.host = nullptr;
        route.original = nullptr;
    }

    if (poseTarget005_) MH_RemoveHook(poseTarget005_);
    if (poseTarget006_ && poseTarget006_ != poseTarget005_) MH_RemoveHook(poseTarget006_);
    if (contextTarget_) {
        MH_RemoveHook(contextTarget_);
    }
    MH_Uninitialize();

    contextTarget_ = nullptr;
    poseTarget005_ = nullptr;
    poseTarget006_ = nullptr;
    originalGetInterface_.store(nullptr, std::memory_order_release);
    originalPose005_.store(nullptr, std::memory_order_release);
    originalPose006_.store(nullptr, std::memory_order_release);
    minHookInitialized_ = false;
}

bool InterfaceHooks::submitPose(std::uint32_t deviceIndex,
                                const vr::DriverPose_t& pose,
                                std::uint64_t generation) noexcept {
    // Synthetic submissions need the same lifetime protection as detours. Do
    // not hold mutex_ across route locking or a host call: reentrant interface
    // discovery acquires mutex_ and would otherwise invert the lock order.
    CallbackRundown callback;
    if (detourShutdown_.load(std::memory_order_seq_cst) ||
        instance_.load(std::memory_order_acquire) != this) return false;
    try {
        if (deviceIndex >= poseRoutes_.size()) {
            return false;
        }
        auto& route = poseRoutes_[deviceIndex];
        std::scoped_lock routeLock(route.mutex);
        vr::IVRServerDriverHost* const host = route.host;
        const PoseFn original = route.original;
        if (!host || !original || !provider_ ||
            !provider_->isCurrentPoseGeneration(deviceIndex, generation)) return false;
        original(host, deviceIndex, pose, sizeof(pose));
        return true;
    } catch (...) {
    }
    return false;
}

void* VR_CALLTYPE InterfaceHooks::detourGetInterface(vr::IVRDriverContext* context,
                                                     const char* version,
                                                     vr::EVRInitError* error) {
    CallbackRundown callback;
    if (detourShutdown_.load(std::memory_order_seq_cst)) {
        if (error) *error = vr::VRInitError_Init_NotInitialized;
        return nullptr;
    }
    const GetInterfaceFn original = originalGetInterface_.load(std::memory_order_acquire);
    void* result = original ? original(context, version, error) : nullptr;
    InterfaceHooks* instance = instance_.load(std::memory_order_acquire);
    if (instance && version && result) {
        try {
            instance->observeInterface(version, result);
        } catch (...) {
            // Interface discovery must never break the driver being loaded.
        }
    }
    return result;
}

void VR_CALLTYPE InterfaceHooks::detourPose005(vr::IVRServerDriverHost* host,
                                               std::uint32_t deviceIndex,
                                               const vr::DriverPose_t* pose,
                                               std::uint32_t poseSize) {
    CallbackRundown callback;
    if (detourShutdown_.load(std::memory_order_seq_cst)) return;
    InterfaceHooks* instance = instance_.load(std::memory_order_acquire);
    const PoseFn original = originalPose005_.load(std::memory_order_acquire);
    if (instance) {
        instance->filterAndForward(original, host, deviceIndex, pose, poseSize);
    } else if (original && pose) {
        original(host, deviceIndex, *pose, poseSize);
    }
}

void VR_CALLTYPE InterfaceHooks::detourPose006(vr::IVRServerDriverHost* host,
                                               std::uint32_t deviceIndex,
                                               const vr::DriverPose_t* pose,
                                               std::uint32_t poseSize) {
    CallbackRundown callback;
    if (detourShutdown_.load(std::memory_order_seq_cst)) return;
    InterfaceHooks* instance = instance_.load(std::memory_order_acquire);
    const PoseFn original = originalPose006_.load(std::memory_order_acquire);
    if (instance) {
        instance->filterAndForward(original, host, deviceIndex, pose, poseSize);
    } else if (original && pose) {
        original(host, deviceIndex, *pose, poseSize);
    }
}

bool InterfaceHooks::installContextHook(vr::IVRDriverContext* context) {
    if (!context || contextTarget_) return contextTarget_ != nullptr;
    contextTarget_ = vtableTarget(context, 0);
    if (!contextTarget_) return false;

    GetInterfaceFn trampoline = nullptr;
    MH_STATUS result = MH_CreateHook(contextTarget_,
        reinterpret_cast<void*>(&detourGetInterface),
        reinterpret_cast<void**>(&trampoline));
    if (result != MH_OK) {
        contextTarget_ = nullptr;
        return false;
    }
    originalGetInterface_.store(trampoline, std::memory_order_release);
    result = MH_EnableHook(contextTarget_);
    if (result != MH_OK) {
        MH_RemoveHook(contextTarget_);
        contextTarget_ = nullptr;
        originalGetInterface_.store(nullptr, std::memory_order_release);
        return false;
    }
    return true;
}

bool InterfaceHooks::installPoseHook(void* interfacePointer, bool version006) {
    if (!interfacePointer) return false;
    void* target = vtableTarget(interfacePointer, 1);
    if (!target) return false;
    // Each detour has one trampoline. A second implementation of the same host
    // version must not replace it while the first implementation is still hooked.
    void* const existingTarget = version006 ? poseTarget006_ : poseTarget005_;
    if (existingTarget && existingTarget != target) return false;
    if (target == poseTarget005_ || target == poseTarget006_) {
        if (version006) {
            poseTarget006_ = target;
            if (!originalPose006_.load(std::memory_order_relaxed)) {
                originalPose006_.store(originalPose005_.load(std::memory_order_relaxed),
                                       std::memory_order_release);
            }
        } else {
            poseTarget005_ = target;
            if (!originalPose005_.load(std::memory_order_relaxed)) {
                originalPose005_.store(originalPose006_.load(std::memory_order_relaxed),
                                       std::memory_order_release);
            }
        }
        return true;
    }

    void* detour = version006 ? reinterpret_cast<void*>(&detourPose006)
                              : reinterpret_cast<void*>(&detourPose005);
    std::atomic<PoseFn>& original = version006 ? originalPose006_ : originalPose005_;
    PoseFn trampoline = nullptr;
    MH_STATUS result = MH_CreateHook(target, detour, reinterpret_cast<void**>(&trampoline));
    if (result != MH_OK) return false;
    original.store(trampoline, std::memory_order_release);
    result = MH_EnableHook(target);
    if (result != MH_OK) {
        MH_RemoveHook(target);
        original.store(nullptr, std::memory_order_release);
        return false;
    }
    if (version006) poseTarget006_ = target;
    else poseTarget005_ = target;
    return true;
}

void InterfaceHooks::observeInterface(const char* version, void* interfacePointer) {
    std::scoped_lock lock(mutex_);
    if (shuttingDown_) return;
    if (std::strcmp(version, "IVRServerDriverHost_006") == 0) {
        const bool newlyObserved = poseTarget006_ == nullptr;
        if (installPoseHook(interfacePointer, true) && newlyObserved && provider_) {
            provider_->log("IVRServerDriverHost_006 pose hook installed");
        }
    } else if (std::strcmp(version, "IVRServerDriverHost_005") == 0) {
        const bool newlyObserved = poseTarget005_ == nullptr;
        if (installPoseHook(interfacePointer, false) && newlyObserved && provider_) {
            provider_->log("IVRServerDriverHost_005 pose hook installed");
        }
    }
}

void InterfaceHooks::filterAndForward(PoseFn original, vr::IVRServerDriverHost* host,
                                      std::uint32_t deviceIndex,
                                      const vr::DriverPose_t* pose,
                                      std::uint32_t poseSize) noexcept {
    if (!original || !pose) return;
    if (!provider_ || poseSize != sizeof(vr::DriverPose_t) ||
        deviceIndex >= vr::k_unMaxTrackedDeviceCount) {
        original(host, deviceIndex, *pose, poseSize);
        return;
    }
    if (!provider_->isFilterTarget(deviceIndex)) {
        original(host, deviceIndex, *pose, poseSize);
        return;
    }

    bool forwarded = false;
    try {
        auto& route = poseRoutes_[deviceIndex];
        std::scoped_lock routeLock(route.mutex);
        route.host = host;
        route.original = original;

        vr::DriverPose_t filtered;
        bool useFiltered = false;
        try {
            useFiltered = provider_->filterPose(deviceIndex, *pose, filtered);
        } catch (...) {
            // Keep the serialization lane until the fallback raw pose is sent.
        }
        forwarded = true;
        original(host, deviceIndex, useFiltered ? filtered : *pose, poseSize);
    } catch (...) {
        // A lock/bookkeeping failure must still forward once. If the host itself
        // throws, never resend a pose it may already have consumed.
    }
    if (!forwarded) original(host, deviceIndex, *pose, poseSize);
}

void* InterfaceHooks::vtableTarget(void* object, int index) noexcept {
    if (!object) return nullptr;
    auto*** typedObject = reinterpret_cast<void***>(object);
    return *typedObject ? (*typedObject)[index] : nullptr;
}

}  // namespace pose_anchor::driver
