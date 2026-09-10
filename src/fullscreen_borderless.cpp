#include "fullscreen_borderless.h"

#include <cstdint>
#include <cstring>

#include "logger.h"

namespace fullscreen_borderless {
namespace {
constexpr uintptr_t kVidWindowed = 0x00A341C8;
constexpr uintptr_t kScreenWidth = 0x009F72C0;
constexpr uintptr_t kScreenHeight = 0x009F72C4;
constexpr uintptr_t kWindowedReference = 0x004D7928;
constexpr uintptr_t kResolutionStore = 0x0046AC27;
const unsigned char kExpectedWindowedReference[] = {0xA1, 0xC8, 0x41, 0xA3, 0x00};
const unsigned char kExpectedResolutionStore[] = {
    0x89, 0x3D, 0xC0, 0x72, 0x9F, 0x00,
    0x89, 0x35, 0xC4, 0x72, 0x9F, 0x00,
};

bool g_enabled = false;
volatile LONG g_applying = 0;
RECT g_monitorRect = {};

bool WriteGameValue(uintptr_t address, LONG value, const char* name) {
    void* destination = reinterpret_cast<void*>(address);
    DWORD oldProtection = 0;
    if (!VirtualProtect(destination, sizeof(value), PAGE_READWRITE, &oldProtection)) {
        logger::Log("ERROR", "FullscreenBorderless",
                    "could not write %s: VirtualProtect error=%lu", name, GetLastError());
        return false;
    }
    std::memcpy(destination, &value, sizeof(value));
    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(destination, sizeof(value), oldProtection, &ignored);
    if (!restored) {
        logger::Log("ERROR", "FullscreenBorderless",
                    "could not restore protection for %s: error=%lu", name, GetLastError());
    }
    return restored != FALSE;
}

bool ValidateExecutable() {
    return std::memcmp(reinterpret_cast<const void*>(kWindowedReference),
                       kExpectedWindowedReference, sizeof(kExpectedWindowedReference)) == 0 &&
           std::memcmp(reinterpret_cast<const void*>(kResolutionStore),
                       kExpectedResolutionStore, sizeof(kExpectedResolutionStore)) == 0;
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
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &info)) return false;
    *result = info.rcMonitor;
    return !IsRectEmpty(result);
}

bool WindowMatches(HWND window, LONG_PTR style, LONG_PTR exStyle) {
    RECT current = {};
    if (!GetWindowRect(window, &current)) return false;
    const LONG_PTR unwantedStyle = WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
                                  WS_MAXIMIZEBOX | WS_SYSMENU;
    const LONG_PTR unwantedExStyle = WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
                                    WS_EX_CLIENTEDGE | WS_EX_STATICEDGE;
    return (style & WS_POPUP) != 0 && (style & unwantedStyle) == 0 &&
           (exStyle & unwantedExStyle) == 0 && EqualRect(&current, &g_monitorRect);
}
}  // namespace

bool Initialize(bool enabled) {
    g_enabled = enabled;
    if (!enabled) {
        logger::Log("INFO", "FullscreenBorderless", "feature disabled");
        return true;
    }
    if (!ValidateExecutable()) {
        logger::Log("ERROR", "FullscreenBorderless",
                    "supported executable signatures did not match; feature disabled");
        g_enabled = false;
        return false;
    }
    if (!GetPrimaryMonitorRect(&g_monitorRect)) {
        logger::Log("ERROR", "FullscreenBorderless",
                    "primary monitor bounds unavailable: error=%lu", GetLastError());
        g_enabled = false;
        return false;
    }
    const LONG width = g_monitorRect.right - g_monitorRect.left;
    const LONG height = g_monitorRect.bottom - g_monitorRect.top;
    const bool configured = WriteGameValue(kVidWindowed, 1, "VID_Windowed") &&
                            WriteGameValue(kScreenWidth, width, "screen width") &&
                            WriteGameValue(kScreenHeight, height, "screen height");
    if (!configured) {
        g_enabled = false;
        return false;
    }
    logger::Log("INFO", "FullscreenBorderless",
                "feature enabled monitor=(%ld,%ld)-(%ld,%ld) resolution=%ldx%ld",
                g_monitorRect.left, g_monitorRect.top, g_monitorRect.right,
                g_monitorRect.bottom, width, height);
    return true;
}

bool Apply(HWND window, const char* trigger) {
    if (!g_enabled) return true;
    if (window == nullptr || !IsWindow(window)) return false;
    if (InterlockedCompareExchange(&g_applying, 1, 0) != 0) return true;

    RECT monitorRect = {};
    if (!GetWindowMonitorRect(window, &monitorRect)) {
        InterlockedExchange(&g_applying, 0);
        logger::Log("ERROR", "FullscreenBorderless",
                    "trigger=%s monitor bounds unavailable: error=%lu", trigger, GetLastError());
        return false;
    }
    g_monitorRect = monitorRect;
    const LONG monitorWidth = monitorRect.right - monitorRect.left;
    const LONG monitorHeight = monitorRect.bottom - monitorRect.top;
    const bool configured = WriteGameValue(kVidWindowed, 1, "VID_Windowed") &&
                            WriteGameValue(kScreenWidth, monitorWidth, "screen width") &&
                            WriteGameValue(kScreenHeight, monitorHeight, "screen height");
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

    const LONG_PTR removedStyle = WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
                                  WS_MAXIMIZEBOX | WS_SYSMENU;
    const LONG_PTR removedExStyle = WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
                                    WS_EX_CLIENTEDGE | WS_EX_STATICEDGE;
    const LONG_PTR style = (oldStyle & ~removedStyle) | WS_POPUP;
    const LONG_PTR exStyle = oldExStyle & ~removedExStyle;

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
    const DWORD positionError = positioned ? ERROR_SUCCESS : GetLastError();
    InterlockedExchange(&g_applying, 0);

    logger::Log(positioned ? "INFO" : "ERROR", "FullscreenBorderless",
                "trigger=%s style=0x%08lX ex_style=0x%08lX rect=(%ld,%ld)-(%ld,%ld) result=%d error=%lu",
                trigger, static_cast<unsigned long>(style), static_cast<unsigned long>(exStyle),
                g_monitorRect.left, g_monitorRect.top, g_monitorRect.right,
                g_monitorRect.bottom, positioned, positionError);
    return positioned != FALSE;
}

}  // namespace fullscreen_borderless
