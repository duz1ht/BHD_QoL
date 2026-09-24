#include "free_rate_mouse_poll.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <limits>

#include "logger.h"
#include "raw_input.h"

namespace free_rate_mouse_poll {
namespace {
constexpr uintptr_t kRenderCameraCall = 0x004B7297;
constexpr uintptr_t kCalculateCameraPositions = 0x0043B5E0;
constexpr unsigned char kExpectedCall[] = {0xE8, 0x44, 0x43, 0xF8, 0xFF};
using CalculateCameraPositionsFn = int(__cdecl*)();
bool g_installed = false;

extern "C" int __cdecl CalculateCameraPositionsRenderWrapper() {
    raw_input::PollForRenderFrame();
    return reinterpret_cast<CalculateCameraPositionsFn>(kCalculateCameraPositions)();
}

bool WriteCall() {
    auto* call = reinterpret_cast<unsigned char*>(kRenderCameraCall);
    if (std::memcmp(call, kExpectedCall, sizeof(kExpectedCall)) != 0) {
        logger::Log("ERROR", "FreeRateMousePoll",
                    "render camera call signature mismatch address=0x%08lX",
                    static_cast<unsigned long>(kRenderCameraCall));
        return false;
    }
    const intptr_t displacement = reinterpret_cast<intptr_t>(&CalculateCameraPositionsRenderWrapper) -
                                  reinterpret_cast<intptr_t>(call + 5);
    if (displacement < std::numeric_limits<int32_t>::min() ||
        displacement > std::numeric_limits<int32_t>::max()) return false;
    DWORD oldProtect = 0;
    if (!VirtualProtect(call, sizeof(kExpectedCall), PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    call[0] = 0xE8;
    const int32_t relative = static_cast<int32_t>(displacement);
    std::memcpy(call + 1, &relative, sizeof(relative));
    FlushInstructionCache(GetCurrentProcess(), call, sizeof(kExpectedCall));
    DWORD ignored = 0;
    VirtualProtect(call, sizeof(kExpectedCall), oldProtect, &ignored);
    return true;
}
} // namespace

bool Install(bool enabled, bool rawInputAvailable) {
    if (!enabled) {
        logger::Log("INFO", "FreeRateMousePoll", "feature disabled");
        return false;
    }
    if (!rawInputAvailable) {
        logger::Log("WARN", "FreeRateMousePoll", "disabled because RawMouseInput is unavailable");
        return false;
    }
    if (g_installed) return true;
    if (!WriteCall()) {
        logger::Log("ERROR", "FreeRateMousePoll", "installation failed; native render call retained");
        return false;
    }
    g_installed = true;
    logger::Log("INFO", "FreeRateMousePoll", "render camera wrapper installed address=0x%08lX",
                static_cast<unsigned long>(kRenderCameraCall));
    return true;
}

bool IsInstalled() { return g_installed; }
} // namespace free_rate_mouse_poll
