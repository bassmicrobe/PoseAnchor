#include "server_provider.hpp"

#include <cstring>
#include <openvr_driver.h>

namespace {
pose_anchor::driver::ServerProvider g_serverProvider;
}

extern "C" __declspec(dllexport) void* HmdDriverFactory(const char* interfaceName,
                                                        int* returnCode) {
    if (interfaceName &&
        std::strcmp(interfaceName, vr::IServerTrackedDeviceProvider_Version) == 0) {
        if (returnCode) *returnCode = vr::VRInitError_None;
        return &g_serverProvider;
    }
    if (returnCode) *returnCode = vr::VRInitError_Init_InterfaceNotFound;
    return nullptr;
}
