#include "borderless_gamma.h"

#include <cstdint>
#include <cstring>

#include "logger.h"

namespace borderless_gamma {
namespace {
constexpr uintptr_t kGameGammaRamp = 0x00E948B0;
constexpr uintptr_t kSetGammaRampCall = 0x005AB346;
const unsigned char kExpectedSetGammaRampCall[] = {
    0xA1, 0x64, 0xE5, 0xE9, 0x00, 0x68, 0xB0, 0x48, 0xE9, 0x00,
    0x6A, 0x00, 0x50, 0x8B, 0x08, 0xDD, 0xD8, 0xFF, 0x51, 0x48,
};

struct GammaRamp {
    WORD red[256];
    WORD green[256];
    WORD blue[256];
};

bool g_enabled = false;
bool g_active = false;
bool g_originalCaptured = false;
HDC g_displayDc = nullptr;
GammaRamp g_originalRamp = {};
GammaRamp g_appliedRamp = {};
wchar_t g_displayName[CCHDEVICENAME] = {};

bool Restore(const char* trigger);

void CloseDisplayDc() {
    if (g_displayDc != nullptr) {
        DeleteDC(g_displayDc);
        g_displayDc = nullptr;
    }
    g_displayName[0] = L'\0';
    g_originalCaptured = false;
}

bool OpenDisplayDc(HWND window) {
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &info)) return false;
    if (g_displayDc != nullptr && lstrcmpW(g_displayName, info.szDevice) == 0) return true;

    Restore("monitor_changed");
    CloseDisplayDc();
    g_displayDc = CreateDCW(L"DISPLAY", info.szDevice, nullptr, nullptr);
    if (g_displayDc == nullptr) return false;
    lstrcpynW(g_displayName, info.szDevice, CCHDEVICENAME);
    if (!GetDeviceGammaRamp(g_displayDc, &g_originalRamp)) {
        CloseDisplayDc();
        return false;
    }
    g_originalCaptured = true;
    logger::Log("INFO", "BorderlessGamma", "captured original ramp display=%ls",
                g_displayName);
    return true;
}

bool Restore(const char* trigger) {
    if (!g_active) return true;
    const BOOL restored = g_displayDc != nullptr && g_originalCaptured &&
                          SetDeviceGammaRamp(g_displayDc, &g_originalRamp);
    logger::Log(restored ? "INFO" : "WARN", "BorderlessGamma",
                "trigger=%s action=restore result=%d error=%lu", trigger, restored,
                restored ? ERROR_SUCCESS : GetLastError());
    g_active = false;
    return restored != FALSE;
}

bool Apply(HWND window, const char* trigger, bool force) {
    if (!g_enabled || window == nullptr || !IsWindow(window) || IsIconic(window) ||
        GetForegroundWindow() != window) return false;
    if (!OpenDisplayDc(window)) {
        logger::Log("WARN", "BorderlessGamma",
                    "trigger=%s could not open display gamma ramp: error=%lu", trigger,
                    GetLastError());
        return false;
    }

    GammaRamp gameRamp = {};
    std::memcpy(&gameRamp, reinterpret_cast<const void*>(kGameGammaRamp), sizeof(gameRamp));
    if (!force && g_active && std::memcmp(&gameRamp, &g_appliedRamp, sizeof(gameRamp)) == 0) {
        return true;
    }
    const BOOL applied = SetDeviceGammaRamp(g_displayDc, &gameRamp);
    if (applied) {
        g_appliedRamp = gameRamp;
        g_active = true;
    }
    logger::Log(applied ? "INFO" : "WARN", "BorderlessGamma",
                "trigger=%s action=apply result=%d error=%lu", trigger, applied,
                applied ? ERROR_SUCCESS : GetLastError());
    return applied != FALSE;
}
}  // namespace

bool Initialize(bool enabled) {
    g_enabled = false;
    if (!enabled) {
        logger::Log("INFO", "BorderlessGamma", "feature disabled");
        return true;
    }
    if (std::memcmp(reinterpret_cast<const void*>(kSetGammaRampCall),
                    kExpectedSetGammaRampCall, sizeof(kExpectedSetGammaRampCall)) != 0) {
        logger::Log("ERROR", "BorderlessGamma",
                    "supported executable signature did not match; feature disabled");
        return false;
    }
    g_enabled = true;
    logger::Log("INFO", "BorderlessGamma", "feature enabled");
    return true;
}

void HandleFocusGained(HWND window, const char* trigger) { Apply(window, trigger, true); }

void HandleFocusLost(const char* trigger) { Restore(trigger); }

void HandleDisplayChanged(HWND window, const char* trigger) {
    Restore(trigger);
    CloseDisplayDc();
    Apply(window, trigger, true);
}

void Poll(HWND window) { Apply(window, "poll", false); }

void Shutdown() {
    Restore("shutdown");
    CloseDisplayDc();
    g_enabled = false;
}

}  // namespace borderless_gamma
