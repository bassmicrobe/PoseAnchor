#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <openvr_driver.h>

namespace pose_anchor::driver {

class ServerProvider;

class InterfaceHooks {
public:
    InterfaceHooks() = default;
    ~InterfaceHooks();

    InterfaceHooks(const InterfaceHooks&) = delete;
    InterfaceHooks& operator=(const InterfaceHooks&) = delete;

    bool install(ServerProvider* provider, vr::IVRDriverContext* context,
                 vr::IVRServerDriverHost* currentHost);
    void remove();
    [[nodiscard]] bool submitPose(std::uint32_t deviceIndex,
                                  const vr::DriverPose_t& pose) noexcept;

private:
    class CallbackRundown {
    public:
        CallbackRundown() noexcept { inFlight_.fetch_add(1, std::memory_order_acq_rel); }
        ~CallbackRundown() { inFlight_.fetch_sub(1, std::memory_order_acq_rel); }
    };

    using GetInterfaceFn = void* (VR_CALLTYPE*)(vr::IVRDriverContext*, const char*,
                                                vr::EVRInitError*);
    using PoseFn = void (VR_CALLTYPE*)(vr::IVRServerDriverHost*, std::uint32_t,
                                      const vr::DriverPose_t&, std::uint32_t);

    struct PoseRoute {
        std::mutex mutex;
        std::atomic_uint64_t sequence{};
        std::atomic<vr::IVRServerDriverHost*> host{nullptr};
        std::atomic<PoseFn> original{nullptr};
    };

    static void* VR_CALLTYPE detourGetInterface(vr::IVRDriverContext* context,
                                                const char* version,
                                                vr::EVRInitError* error);
    static void VR_CALLTYPE detourPose005(vr::IVRServerDriverHost* host,
                                          std::uint32_t deviceIndex,
                                          const vr::DriverPose_t* pose,
                                          std::uint32_t poseSize);
    static void VR_CALLTYPE detourPose006(vr::IVRServerDriverHost* host,
                                          std::uint32_t deviceIndex,
                                          const vr::DriverPose_t* pose,
                                          std::uint32_t poseSize);

    bool installContextHook(vr::IVRDriverContext* context);
    bool installPoseHook(void* interfacePointer, bool version006);
    void observeInterface(const char* version, void* interfacePointer);
    void filterAndForward(PoseFn original, vr::IVRServerDriverHost* host,
                          std::uint32_t deviceIndex, const vr::DriverPose_t* pose,
                          std::uint32_t poseSize) noexcept;
    static void* vtableTarget(void* object, int index) noexcept;

    static std::atomic<InterfaceHooks*> instance_;
    static std::atomic_uint32_t inFlight_;
    // Checked by every detour after its inFlight_ increment. A thread that entered a
    // detour before remove() raised the flag either becomes visible to the drain loop
    // or observes the flag and returns before touching a trampoline remove() frees.
    static std::atomic_bool detourShutdown_;
    static std::atomic<GetInterfaceFn> originalGetInterface_;
    static std::atomic<PoseFn> originalPose005_;
    static std::atomic<PoseFn> originalPose006_;

    std::mutex mutex_;
    ServerProvider* provider_{};
    void* contextTarget_{};
    void* poseTarget005_{};
    void* poseTarget006_{};
    bool minHookInitialized_{};
    bool shuttingDown_{};
    std::array<PoseRoute, vr::k_unMaxTrackedDeviceCount> poseRoutes_{};
};

}  // namespace pose_anchor::driver
