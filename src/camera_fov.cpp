#include "camera_fov.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "borderless_fullscreen.h"
#include "camera_fov_math.h"
#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
#endif

#include "logger.h"

namespace camera_fov {
namespace {
constexpr uintptr_t kBuildCameraRva = 0x000181A0;
constexpr uintptr_t kMainRendererReturnRva = 0x000B751F;
constexpr uintptr_t kCurrentFovRva = 0x007635A0;
constexpr uintptr_t kTargetFovRva = 0x007635A4;
// Camera mode lives at VA 0x007F2DD0 in the supported image (base 0x00400000).
constexpr uintptr_t kCameraModeRva = 0x003F2DD0;
constexpr uintptr_t kRenderWidthRva = 0x005F72C0;
constexpr uintptr_t kRenderHeightRva = 0x005F72C4;
constexpr size_t kCameraFovOffset = 0x3C;
constexpr size_t kHookLength = 9;

const unsigned char kBuildCameraSignature[kHookLength] = {
    0x55,                         // push ebp
    0x8B, 0xEC,                   // mov ebp,esp
    0x81, 0xEC, 0x40, 0x02, 0x00, 0x00, // sub esp,0x240
};

using BuildCameraFn = void(__cdecl *)(void *, void *);

BuildCameraFn g_originalBuildCamera = nullptr;
uintptr_t g_moduleBase = 0;
bool g_installed = false;
LONG g_cachedWidth = -1;
LONG g_cachedHeight = -1;
int32_t g_cachedFirstPersonFov = kVanillaFovQ16;

void GetDisplayedSize(LONG* width, LONG* height) {
    if (borderless_fullscreen::GetOutputSize(width, height)) return;
    *width = *reinterpret_cast<const volatile LONG*>(g_moduleBase + kRenderWidthRva);
    *height = *reinterpret_cast<const volatile LONG*>(g_moduleBase + kRenderHeightRva);
}

int32_t GetCorrectedFov() {
    LONG width = 0;
    LONG height = 0;
    GetDisplayedSize(&width, &height);
    if (width == g_cachedWidth && height == g_cachedHeight) {
        return g_cachedFirstPersonFov;
    }
    g_cachedWidth = width;
    g_cachedHeight = height;
    g_cachedFirstPersonFov = CorrectHorizontalFovQ16(width, height);
    logger::Log("INFO", "UseCorrectAspectFOV",
                "display_size=%ldx%ld visual_fov=%.2f gameplay_fov=80",
                width, height,
                static_cast<double>(g_cachedFirstPersonFov) / 65536.0);
    return g_cachedFirstPersonFov;
}

bool WriteRelativeJump(unsigned char *output, const void *target) {
    const intptr_t displacement =
        reinterpret_cast<intptr_t>(target) - reinterpret_cast<intptr_t>(output + 5);
    if (displacement < std::numeric_limits<int32_t>::min() ||
        displacement > std::numeric_limits<int32_t>::max()) {
        return false;
    }

    output[0] = 0xE9;
    const int32_t relative = static_cast<int32_t>(displacement);
    memcpy(output + 1, &relative, sizeof(relative));
    return true;
}

void __cdecl HookBuildCamera(void *destinationCamera, void *sourceCamera) {
#if defined(_MSC_VER)
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
#else
    const uintptr_t caller =
        reinterpret_cast<uintptr_t>(__builtin_return_address(0));
#endif
    int32_t *cameraFov = sourceCamera == nullptr
                             ? nullptr
                             : reinterpret_cast<int32_t *>(
                                   static_cast<unsigned char *>(sourceCamera) + kCameraFovOffset);
    const auto *currentFov =
        reinterpret_cast<const volatile int32_t *>(g_moduleBase + kCurrentFovRva);
    const auto *targetFov =
        reinterpret_cast<const volatile int32_t *>(g_moduleBase + kTargetFovRva);
    const auto *cameraMode =
        reinterpret_cast<const volatile int32_t *>(g_moduleBase + kCameraModeRva);

    const bool overrideFov =
        caller == g_moduleBase + kMainRendererReturnRva && cameraFov != nullptr &&
        *cameraMode == 0 &&
        *currentFov == kVanillaFovQ16 && *targetFov == kVanillaFovQ16 &&
        *cameraFov == kVanillaFovQ16;
    const int32_t originalFov = overrideFov ? *cameraFov : 0;
    if (overrideFov) {
        *cameraFov = GetCorrectedFov();
    }

    g_originalBuildCamera(destinationCamera, sourceCamera);

    if (overrideFov) {
        *cameraFov = originalFov;
    }
}

bool InstallHook(unsigned char *hook) {
    auto *trampoline = static_cast<unsigned char *>(VirtualAlloc(
        nullptr, kHookLength + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        return false;
    }

    memcpy(trampoline, hook, kHookLength);
    if (!WriteRelativeJump(trampoline + kHookLength, hook + kHookLength)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, kHookLength, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    g_originalBuildCamera = reinterpret_cast<BuildCameraFn>(trampoline);
    const bool jumpWritten = WriteRelativeJump(hook, reinterpret_cast<const void *>(&HookBuildCamera));
    if (jumpWritten) {
        memset(hook + 5, 0x90, kHookLength - 5);
        FlushInstructionCache(GetCurrentProcess(), hook, kHookLength);
    }
    DWORD ignored = 0;
    VirtualProtect(hook, kHookLength, oldProtect, &ignored);

    if (!jumpWritten) {
        g_originalBuildCamera = nullptr;
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    return true;
}
} // namespace

bool Install(bool enabled) {
    if (!enabled) {
        logger::Log("INFO", "UseCorrectAspectFOV", "feature disabled");
        return false;
    }
    if (g_installed) {
        logger::Log("INFO", "UseCorrectAspectFOV", "hook already installed");
        return true;
    }

    static_assert(sizeof(void *) == 4, "UseCorrectAspectFOV requires a 32-bit build");
    g_moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto *hook = reinterpret_cast<unsigned char *>(g_moduleBase + kBuildCameraRva);
    if (memcmp(hook, kBuildCameraSignature, sizeof(kBuildCameraSignature)) != 0) {
        logger::Log("ERROR", "UseCorrectAspectFOV",
                    "camera builder signature mismatch address=0x%08lX",
                    reinterpret_cast<unsigned long>(hook));
        return false;
    }
    if (!InstallHook(hook)) {
        logger::Log("ERROR", "UseCorrectAspectFOV", "hook installation failed: error=%lu",
                    GetLastError());
        return false;
    }

    g_installed = true;
    logger::Log("INFO", "UseCorrectAspectFOV",
                "hook installed address=0x%08lX reference_fov=80 reference_aspect=4:3",
                reinterpret_cast<unsigned long>(hook));
    return true;
}

} // namespace camera_fov
