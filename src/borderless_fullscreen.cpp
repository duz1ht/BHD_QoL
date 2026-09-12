#include "borderless_fullscreen.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "logger.h"

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
constexpr uintptr_t kSetVideoMode = 0x0046AA10;
constexpr uintptr_t kSelectedVideoMode = 0x00A3421C;
const unsigned char kExpectedWindowedReference[] = {0xA1, 0xC8, 0x41, 0xA3, 0x00};
const unsigned char kExpectedWindowedArgumentHandler[] = {
    0x89, 0x3D, 0x7C, 0xD4, 0x95, 0x00, 0x89, 0x3D, 0xF0, 0x46, 0xA3, 0x00};
const unsigned char kExpectedWindowedRenderRead[] = {0x8B, 0x1D, 0x7C, 0xD4, 0x95, 0x00};
const unsigned char kExpectedResolutionOverride[] = {0xA1, 0x80, 0xD4, 0x95, 0x00};
const unsigned char kExpectedSetVideoMode[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x28, 0x53,
};

bool g_enabled = false;
bool g_forceDesktopResolution = false;
volatile LONG g_applying = 0;
volatile LONG g_monitorWidth = 0;
volatile LONG g_monitorHeight = 0;
volatile LONG g_resolutionChangePending = 0;
RECT g_monitorRect = {};
HMONITOR g_monitor = nullptr;
void* g_resolutionCodeCave = nullptr;
LONG g_lastActive = -1;
LONG g_lastRenderWidth = -1;
LONG g_lastRenderHeight = -1;
LONG g_lastOutputWidth = -1;
LONG g_lastOutputHeight = -1;
bool g_loggedUnknownResolution = false;

bool WriteGameValue(uintptr_t address, LONG value, const char* name) {
    void* destination = reinterpret_cast<void*>(address);
    DWORD oldProtection = 0;
    if (!VirtualProtect(destination, sizeof(value), PAGE_READWRITE, &oldProtection)) {
        logger::Log("ERROR", "BorderlessFullscreen",
                    "could not write %s: VirtualProtect error=%lu", name, GetLastError());
        return false;
    }
    std::memcpy(destination, &value, sizeof(value));
    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(destination, sizeof(value), oldProtection, &ignored);
    if (!restored) {
        logger::Log("ERROR", "BorderlessFullscreen",
                    "could not restore protection for %s: error=%lu", name, GetLastError());
    }
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
            (std::memcmp(reinterpret_cast<const void*>(kResolutionOverride),
                         kExpectedResolutionOverride, sizeof(kExpectedResolutionOverride)) == 0 &&
             std::memcmp(reinterpret_cast<const void*>(kSetVideoMode),
                         kExpectedSetVideoMode, sizeof(kExpectedSetVideoMode)) == 0));
}

bool WriteCode(uintptr_t address, const void* bytes, size_t size) {
    void* destination = reinterpret_cast<void*>(address);
    DWORD oldProtection = 0;
    if (!VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &oldProtection)) {
        logger::Log("ERROR", "BorderlessFullscreen",
                    "could not make resolution patch writable: error=%lu", GetLastError());
        return false;
    }
    std::memcpy(destination, bytes, size);
    FlushInstructionCache(GetCurrentProcess(), destination, size);
    DWORD ignored = 0;
    if (!VirtualProtect(destination, size, oldProtection, &ignored)) {
        logger::Log("WARN", "BorderlessFullscreen",
                    "resolution patch applied but protection restore failed: error=%lu",
                    GetLastError());
    }
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
    const bool windowedConfigured = WriteGameValue(kVidWindowed, 1, "VID_Windowed") &&
                                    WriteGameValue(kCommandLineWindowed, 1,
                                                   "command-line windowed state") &&
                                    WriteGameValue(kWindowedAuxiliary, 1,
                                                   "windowed auxiliary state");
    if (!windowedConfigured || !g_forceDesktopResolution) return windowedConfigured;
    return WriteGameValue(kScreenWidth, g_monitorWidth, "screen width") &&
           WriteGameValue(kScreenHeight, g_monitorHeight, "screen height");
}

void LogActiveState(HWND window, const char* trigger) {
    RECT outer = {};
    RECT client = {};
    POINT upperLeft = {};
    POINT lowerRight = {};
    const bool outerValid = GetWindowRect(window, &outer) != FALSE;
    const bool clientValid = GetClientRect(window, &client) != FALSE;
    lowerRight = {client.right, client.bottom};
    const bool clientScreenValid = clientValid && ClientToScreen(window, &upperLeft) &&
                                   ClientToScreen(window, &lowerRight);
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    const LONG_PTR unwantedStyle = WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
                                  WS_MAXIMIZEBOX | WS_SYSMENU;
    const LONG_PTR unwantedExStyle = WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
                                    WS_EX_CLIENTEDGE | WS_EX_STATICEDGE;
    const bool styleOk = (style & WS_POPUP) && !(style & unwantedStyle) &&
                         !(exStyle & unwantedExStyle);
    const bool outerOk = outerValid && EqualRect(&outer, &g_monitorRect);
    const RECT clientScreen = {upperLeft.x, upperLeft.y, lowerRight.x, lowerRight.y};
    const bool clientOk = clientScreenValid && EqualRect(&clientScreen, &g_monitorRect);
    const bool windowedOk = *reinterpret_cast<volatile LONG*>(kCommandLineWindowed) == 1 &&
                            *reinterpret_cast<volatile LONG*>(kWindowedAuxiliary) == 1;
    const LONG renderWidth = *reinterpret_cast<volatile LONG*>(kScreenWidth);
    const LONG renderHeight = *reinterpret_cast<volatile LONG*>(kScreenHeight);
    const LONG outputWidth = g_monitorRect.right - g_monitorRect.left;
    const LONG outputHeight = g_monitorRect.bottom - g_monitorRect.top;
    const bool resolutionKnown = renderWidth > 0 && renderHeight > 0;
    const bool resolutionOk = !g_forceDesktopResolution ||
                              (resolutionKnown && renderWidth == outputWidth &&
                               renderHeight == outputHeight);
    const bool scaled = resolutionKnown &&
                        (renderWidth != outputWidth || renderHeight != outputHeight);
    const LONG active = styleOk && outerOk && clientOk && windowedOk && resolutionOk;
    if (!resolutionKnown && g_loggedUnknownResolution && active == g_lastActive &&
        outputWidth == g_lastOutputWidth && outputHeight == g_lastOutputHeight) return;
    if (resolutionKnown && active == g_lastActive && renderWidth == g_lastRenderWidth &&
        renderHeight == g_lastRenderHeight && outputWidth == g_lastOutputWidth &&
        outputHeight == g_lastOutputHeight) return;
    g_lastActive = active;
    g_lastRenderWidth = resolutionKnown ? renderWidth : -1;
    g_lastRenderHeight = resolutionKnown ? renderHeight : -1;
    g_lastOutputWidth = outputWidth;
    g_lastOutputHeight = outputHeight;
    g_loggedUnknownResolution = !resolutionKnown;
    if (resolutionKnown) {
        logger::Log(active ? "INFO" : "WARN", "BorderlessFullscreen",
                    "trigger=%s active=%ld style_ok=%d window_rect_ok=%d client_rect_ok=%d "
                    "windowed_ok=%d resolution_ok=%d force_desktop_resolution=%d "
                    "render_resolution=%ldx%ld output_size=%ldx%ld scaled=%d",
                    trigger, active, styleOk, outerOk, clientOk, windowedOk, resolutionOk,
                    g_forceDesktopResolution, renderWidth, renderHeight, outputWidth,
                    outputHeight, scaled);
    } else {
        logger::Log(active ? "INFO" : "WARN", "BorderlessFullscreen",
                    "trigger=%s active=%ld style_ok=%d window_rect_ok=%d client_rect_ok=%d "
                    "windowed_ok=%d resolution_ok=%d force_desktop_resolution=%d "
                    "render_resolution=unknown output_size=%ldx%ld scaled=unknown",
                    trigger, active, styleOk, outerOk, clientOk, windowedOk, resolutionOk,
                    g_forceDesktopResolution, outputWidth, outputHeight);
    }
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

bool Initialize(bool enabled, bool forceDesktopResolution) {
    g_enabled = enabled;
    g_forceDesktopResolution = enabled && forceDesktopResolution;
    if (!enabled) {
        logger::Log("INFO", "BorderlessFullscreen", "feature disabled");
        return true;
    }
    if (!ValidateExecutable(g_forceDesktopResolution)) {
        logger::Log("ERROR", "BorderlessFullscreen",
                    "supported executable signatures did not match; feature disabled");
        g_enabled = false;
        return false;
    }
    if (!GetPrimaryMonitorRect(&g_monitorRect)) {
        logger::Log("ERROR", "BorderlessFullscreen",
                    "primary monitor bounds unavailable: error=%lu", GetLastError());
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
        logger::Log("ERROR", "BorderlessFullscreen",
                    "windowed state or requested resolution override installation failed");
        g_enabled = false;
        return false;
    }
    logger::Log("INFO", "BorderlessFullscreen",
                "feature enabled force_desktop_resolution=%d "
                "monitor=(%ld,%ld)-(%ld,%ld) output_size=%ldx%ld",
                g_forceDesktopResolution,
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
        logger::Log("ERROR", "BorderlessFullscreen",
                    "trigger=%s monitor bounds unavailable: error=%lu", trigger, GetLastError());
        return false;
    }
    const LONG monitorWidth = monitorRect.right - monitorRect.left;
    const LONG monitorHeight = monitorRect.bottom - monitorRect.top;
    const LONG previousWidth = InterlockedCompareExchange(&g_monitorWidth, 0, 0);
    const LONG previousHeight = InterlockedCompareExchange(&g_monitorHeight, 0, 0);
    const bool dimensionsChanged = previousWidth > 0 && previousHeight > 0 &&
                                   (monitorWidth != previousWidth ||
                                    monitorHeight != previousHeight);
    g_monitorRect = monitorRect;
    InterlockedExchange(&g_monitorWidth, monitorWidth);
    InterlockedExchange(&g_monitorHeight, monitorHeight);
    if (g_forceDesktopResolution && dimensionsChanged) {
        InterlockedExchange(&g_resolutionChangePending, 1);
        logger::Log("INFO", "BorderlessFullscreen",
                    "trigger=%s device_reset=pending old_output=%ldx%ld new_output=%ldx%ld",
                    trigger, previousWidth, previousHeight, monitorWidth, monitorHeight);
    }
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
        LogActiveState(window, trigger);
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

    logger::Log(positioned ? "INFO" : "ERROR", "BorderlessFullscreen",
                "trigger=%s style=0x%08lX ex_style=0x%08lX rect=(%ld,%ld)-(%ld,%ld) result=%d error=%lu",
                trigger, static_cast<unsigned long>(style), static_cast<unsigned long>(exStyle),
                g_monitorRect.left, g_monitorRect.top, g_monitorRect.right,
                g_monitorRect.bottom, positioned, positionError);
    if (positioned) LogActiveState(window, trigger);
    return positioned != FALSE;
}

bool ProcessPendingResolutionChange(HWND window, const char* trigger) {
    if (!g_enabled || !g_forceDesktopResolution) return true;
    if (InterlockedCompareExchange(&g_resolutionChangePending, 0, 1) != 1) return true;
    if (window == nullptr || !IsWindow(window) || IsIconic(window)) {
        InterlockedExchange(&g_resolutionChangePending, 1);
        return false;
    }

    // SetVideoMode is the game's normal D3D8 teardown/recreation path. Calling
    // it rather than IDirect3DDevice8::Reset directly also rebuilds all of the
    // game's default-pool resources and viewport state. The installed code cave
    // substitutes the current monitor dimensions when this routine selects its
    // back-buffer size.
    using SetVideoMode = int (__cdecl*)(int);
    const int selectedMode = *reinterpret_cast<volatile int*>(kSelectedVideoMode);
    const int result = reinterpret_cast<SetVideoMode>(kSetVideoMode)(selectedMode);
    const LONG width = InterlockedCompareExchange(&g_monitorWidth, 0, 0);
    const LONG height = InterlockedCompareExchange(&g_monitorHeight, 0, 0);
    logger::Log(result ? "INFO" : "ERROR", "BorderlessFullscreen",
                "trigger=%s device_reset=%s selected_mode=%d requested_backbuffer=%ldx%ld",
                trigger, result ? "completed" : "failed", selectedMode, width, height);
    if (!result) InterlockedExchange(&g_resolutionChangePending, 1);
    return result != 0;
}

bool GetOutputSize(LONG* width, LONG* height) {
    if (!g_enabled || width == nullptr || height == nullptr) return false;
    const LONG currentWidth = InterlockedCompareExchange(&g_monitorWidth, 0, 0);
    const LONG currentHeight = InterlockedCompareExchange(&g_monitorHeight, 0, 0);
    if (currentWidth <= 0 || currentHeight <= 0) return false;
    *width = currentWidth;
    *height = currentHeight;
    return true;
}

}  // namespace borderless_fullscreen
