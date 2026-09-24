#include "free_rate_mouse_poll.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <limits>

#include "logger.h"
#include "raw_input.h"

namespace free_rate_mouse_poll {
namespace {
constexpr uintptr_t kCalculateCameraPositions = 0x0043B5E0;
constexpr uintptr_t kRenderCameraCallSite = 0x004B7297;
constexpr unsigned char kExpectedCall[] = {0xE8, 0x44, 0x43, 0xF8, 0xFF};
using CalculateCameraPositionsFn = int(__cdecl*)();

extern "C" int __cdecl CalculateCameraPositions_FreeRatePollHook() {
    raw_input::PollForRenderFrame();
    return reinterpret_cast<CalculateCameraPositionsFn>(kCalculateCameraPositions)();
}
}  // namespace

bool Install(bool enabled) {
    if (!enabled) {
        logger::Log("INFO", "FreeRateMousePoll", "feature disabled");
        return true;
    }
    if (!raw_input::IsEnabled()) {
        logger::Log("ERROR", "FreeRateMousePoll",
                    "installation skipped; RawMouseInput is unavailable");
        return false;
    }

    auto* callSite = reinterpret_cast<unsigned char*>(kRenderCameraCallSite);
    if (std::memcmp(callSite, kExpectedCall, sizeof(kExpectedCall)) != 0) {
        logger::Log("ERROR", "FreeRateMousePoll",
                    "unexpected CalculateCameraPositions call bytes at 0x%08lX",
                    static_cast<unsigned long>(kRenderCameraCallSite));
        return false;
    }

    const intptr_t displacement = reinterpret_cast<intptr_t>(
        &CalculateCameraPositions_FreeRatePollHook) -
        static_cast<intptr_t>(kRenderCameraCallSite + sizeof(kExpectedCall));
    if (displacement < std::numeric_limits<int32_t>::min() ||
        displacement > std::numeric_limits<int32_t>::max()) {
        logger::Log("ERROR", "FreeRateMousePoll", "hook target is outside relative-call range");
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(callSite, sizeof(kExpectedCall), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        logger::Log("ERROR", "FreeRateMousePoll", "VirtualProtect failed: error=%lu",
                    GetLastError());
        return false;
    }
    callSite[0] = 0xE8;
    *reinterpret_cast<int32_t*>(callSite + 1) = static_cast<int32_t>(displacement);
    FlushInstructionCache(GetCurrentProcess(), callSite, sizeof(kExpectedCall));
    DWORD ignored = 0;
    VirtualProtect(callSite, sizeof(kExpectedCall), oldProtect, &ignored);
    logger::Log("INFO", "FreeRateMousePoll",
                "render-frame PollMouseInput hook installed at 0x%08lX",
                static_cast<unsigned long>(kRenderCameraCallSite));
    return true;
}

}  // namespace free_rate_mouse_poll
