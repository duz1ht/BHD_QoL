#include "borderless_fullscreen.h"

#include <cstddef>
#include <cstdint>
#include <cstring>


namespace borderless_fullscreen {
namespace {
constexpr uintptr_t kVidWindowed = 0x00A341C8;
constexpr uintptr_t kCommandLineWindowed = 0x0095D47C;
constexpr uintptr_t kWindowedAuxiliary = 0x00A346F0;
constexpr uintptr_t kScreenWidth = 0x009F72C0;
constexpr uintptr_t kScreenHeight = 0x009F72C4;
constexpr uintptr_t kWindowedReference = 0x004D7928;
constexpr uintptr_t kWindowedArgumentHandler = 0x00469122;
constexpr uintptr_t kWindowedRenderRead = 0x0046AA17;
constexpr uintptr_t kResolutionOverride = 0x0046AAA9;
constexpr uintptr_t kResolutionOverrideReturn = 0x0046AAAE;
constexpr LONG_PTR kRemovedStyle = WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
                                   WS_MAXIMIZEBOX | WS_SYSMENU;
constexpr LONG_PTR kRemovedExStyle = WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
                                     WS_EX_CLIENTEDGE | WS_EX_STATICEDGE;
const unsigned char kExpectedWindowedReference[] = {0xA1, 0xC8, 0x41, 0xA3, 0x00};
const unsigned char kExpectedWindowedArgumentHandler[] = {
    0x89, 0x3D, 0x7C, 0xD4, 0x95, 0x00, 0x89, 0x3D, 0xF0, 0x46, 0xA3, 0x00};
const unsigned char kExpectedWindowedRenderRead[] = {0x8B, 0x1D, 0x7C, 0xD4, 0x95, 0x00};
const unsigned char kExpectedResolutionOverride[] = {0xA1, 0x80, 0xD4, 0x95, 0x00};

bool g_enabled = false;
bool g_forceDesktopResolution = false;
volatile LONG g_applying = 0;
volatile LONG g_monitorWidth = 0;
volatile LONG g_monitorHeight = 0;
RECT g_monitorRect = {};
HMONITOR g_monitor = nullptr;
void* g_resolutionCodeCave = nullptr;

bool WriteGameValue(uintptr_t address, LONG value) {
    void* destination = reinterpret_cast<void*>(address);
    DWORD oldProtection = 0;
    if (!VirtualProtect(destination, sizeof(value), PAGE_READWRITE, &oldProtection)) {
        return false;
    }
    std::memcpy(destination, &value, sizeof(value));
    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(destination, sizeof(value), oldProtection, &ignored);
    return restored != FALSE;
}

bool ValidateExecutable(bool validateResolutionOverride) {
    const bool windowedValid =
        std::memcmp(reinterpret_cast<const void*>(kWindowedReference),
                    kExpectedWindowedReference, sizeof(kExpectedWindowedReference)) == 0 &&
        std::memcmp(reinterpret_cast<const void*>(kWindowedArgumentHandler),
                    kExpectedWindowedArgumentHandler,
                    sizeof(kExpectedWindowedArgumentHandler)) == 0 &&
        std::memcmp(reinterpret_cast<const void*>(kWindowedRenderRead),
                    kExpectedWindowedRenderRead, sizeof(kExpectedWindowedRenderRead)) == 0;
    return windowedValid &&
           (!validateResolutionOverride ||
            std::memcmp(reinterpret_cast<const void*>(kResolutionOverride),
                        kExpectedResolutionOverride, sizeof(kExpectedResolutionOverride)) == 0);
}

bool WriteCode(uintptr_t address, const void* bytes, size_t size) {
    void* destination = reinterpret_cast<void*>(address);
    DWORD oldProtection = 0;
    if (!VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &oldProtection)) {
        return false;
    }
    std::memcpy(destination, bytes, size);
    FlushInstructionCache(GetCurrentProcess(), destination, size);
    DWORD ignored = 0;
    VirtualProtect(destination, size, oldProtection, &ignored);
    return true;
}

bool InstallResolutionOverride() {
    // MOV EDI,[g_monitorWidth]; MOV ESI,[g_monitorHeight]; displaced MOV EAX,[0095D480]; JMP back.
    unsigned char code[] = {
        0x8B, 0x3D, 0, 0, 0, 0,
        0x8B, 0x35, 0, 0, 0, 0,
        0xA1, 0x80, 0xD4, 0x95, 0x00,
        0xE9, 0, 0, 0, 0,
    };
    g_resolutionCodeCave = VirtualAlloc(nullptr, sizeof(code), MEM_COMMIT | MEM_RESERVE,
                                        PAGE_EXECUTE_READWRITE);
    if (g_resolutionCodeCave == nullptr) return false;
    const uint32_t widthAddress = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(&g_monitorWidth));
    const uint32_t heightAddress = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(&g_monitorHeight));
    std::memcpy(code + 2, &widthAddress, sizeof(widthAddress));
    std::memcpy(code + 8, &heightAddress, sizeof(heightAddress));
    const int32_t returnOffset = static_cast<int32_t>(
        kResolutionOverrideReturn - (reinterpret_cast<uintptr_t>(g_resolutionCodeCave) + sizeof(code)));
    std::memcpy(code + 18, &returnOffset, sizeof(returnOffset));
    std::memcpy(g_resolutionCodeCave, code, sizeof(code));
    FlushInstructionCache(GetCurrentProcess(), g_resolutionCodeCave, sizeof(code));
    DWORD oldProtection = 0;
    if (!VirtualProtect(g_resolutionCodeCave, sizeof(code), PAGE_EXECUTE_READ, &oldProtection)) {
        VirtualFree(g_resolutionCodeCave, 0, MEM_RELEASE);
        g_resolutionCodeCave = nullptr;
        return false;
    }

    unsigned char jump[] = {0xE9, 0, 0, 0, 0};
    const int32_t caveOffset = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(g_resolutionCodeCave) - (kResolutionOverride + sizeof(jump)));
    std::memcpy(jump + 1, &caveOffset, sizeof(caveOffset));
    if (!WriteCode(kResolutionOverride, jump, sizeof(jump))) {
        VirtualFree(g_resolutionCodeCave, 0, MEM_RELEASE);
        g_resolutionCodeCave = nullptr;
        return false;
    }
    return true;
}

bool GetPrimaryMonitorRect(RECT* result) {
    if (result == nullptr) return false;
    const POINT origin = {0, 0};
    const HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &info)) return false;
    *result = info.rcMonitor;
    return !IsRectEmpty(result);
}

bool GetWindowMonitorRect(HWND window, RECT* result) {
    if (result == nullptr) return false;
    if (g_monitor == nullptr) {
        g_monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    }
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (g_monitor == nullptr || !GetMonitorInfoW(g_monitor, &info)) {
        g_monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        if (g_monitor == nullptr || !GetMonitorInfoW(g_monitor, &info)) return false;
    }
    *result = info.rcMonitor;
    return !IsRectEmpty(result);
}

bool ConfigureGame() {
    const bool windowedConfigured = WriteGameValue(kVidWindowed, 1) &&
                                    WriteGameValue(kCommandLineWindowed, 1) &&
                                    WriteGameValue(kWindowedAuxiliary, 1);
    if (!windowedConfigured || !g_forceDesktopResolution) return windowedConfigured;
    return WriteGameValue(kScreenWidth, g_monitorWidth) &&
           WriteGameValue(kScreenHeight, g_monitorHeight);
}


bool WindowMatches(HWND window, LONG_PTR style, LONG_PTR exStyle) {
    RECT current = {};
    if (!GetWindowRect(window, &current)) return false;
    return (style & WS_POPUP) != 0 && (style & kRemovedStyle) == 0 &&
           (exStyle & kRemovedExStyle) == 0 && EqualRect(&current, &g_monitorRect);
}
}  // namespace

bool Initialize(bool enabled, bool forceDesktopResolution) {
    g_enabled = enabled;
    g_forceDesktopResolution = enabled && forceDesktopResolution;
    if (!enabled) {
        return true;
    }
    if (!ValidateExecutable(g_forceDesktopResolution)) {
        g_enabled = false;
        return false;
    }
    if (!GetPrimaryMonitorRect(&g_monitorRect)) {
        g_enabled = false;
        return false;
    }
    const LONG width = g_monitorRect.right - g_monitorRect.left;
    const LONG height = g_monitorRect.bottom - g_monitorRect.top;
    InterlockedExchange(&g_monitorWidth, width);
    InterlockedExchange(&g_monitorHeight, height);
    const bool configured = ConfigureGame() &&
                            (!g_forceDesktopResolution || InstallResolutionOverride());
    if (!configured) {
        g_enabled = false;
        return false;
    }
    return true;
}

bool Apply(HWND window) {
    if (!g_enabled) return true;
    if (window == nullptr || !IsWindow(window)) return false;
    if (InterlockedCompareExchange(&g_applying, 1, 0) != 0) return true;

    RECT monitorRect = {};
    if (!GetWindowMonitorRect(window, &monitorRect)) {
        InterlockedExchange(&g_applying, 0);
        return false;
    }
    g_monitorRect = monitorRect;
    const LONG monitorWidth = monitorRect.right - monitorRect.left;
    const LONG monitorHeight = monitorRect.bottom - monitorRect.top;
    InterlockedExchange(&g_monitorWidth, monitorWidth);
    InterlockedExchange(&g_monitorHeight, monitorHeight);
    const bool configured = ConfigureGame();
    if (!configured) {
        InterlockedExchange(&g_applying, 0);
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR oldStyle = GetWindowLongPtrW(window, GWL_STYLE);
    if (oldStyle == 0 && GetLastError() != ERROR_SUCCESS) {
        InterlockedExchange(&g_applying, 0);
        return false;
    }
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR oldExStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (oldExStyle == 0 && GetLastError() != ERROR_SUCCESS) {
        InterlockedExchange(&g_applying, 0);
        return false;
    }
    if (WindowMatches(window, oldStyle, oldExStyle)) {
        InterlockedExchange(&g_applying, 0);
        return true;
    }

    const LONG_PTR style = (oldStyle & ~kRemovedStyle) | WS_POPUP;
    const LONG_PTR exStyle = oldExStyle & ~kRemovedExStyle;

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previousStyle = SetWindowLongPtrW(window, GWL_STYLE, style);
    const bool styleSet = previousStyle != 0 || GetLastError() == ERROR_SUCCESS;
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previousExStyle = SetWindowLongPtrW(window, GWL_EXSTYLE, exStyle);
    const bool exStyleSet = previousExStyle != 0 || GetLastError() == ERROR_SUCCESS;
    const int width = g_monitorRect.right - g_monitorRect.left;
    const int height = g_monitorRect.bottom - g_monitorRect.top;
    const BOOL positioned = styleSet && exStyleSet &&
        SetWindowPos(window, HWND_TOP, g_monitorRect.left, g_monitorRect.top, width, height,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
    InterlockedExchange(&g_applying, 0);

    return positioned != FALSE;
}

}  // namespace borderless_fullscreen
