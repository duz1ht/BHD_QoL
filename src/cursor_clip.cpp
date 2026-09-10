#include "cursor_clip.h"

#include <cstdint>
#include <cstdlib>

#include "logger.h"

namespace cursor_clip {
namespace {
constexpr uintptr_t kScreenWidth = 0x009F72C0;
constexpr uintptr_t kScreenHeight = 0x009F72C4;

bool g_enabled = false;
bool g_ready = false;
bool g_recoveryPending = false;
HWND g_window = nullptr;
RECT g_lastValidClip = {};
bool g_hasLastValidClip = false;
LONG g_lastOnOtherMonitor = -1;

bool GetClientScreenRect(RECT* result) {
    if (result == nullptr || g_window == nullptr || !IsWindow(g_window)) return false;
    RECT client = {};
    if (!GetClientRect(g_window, &client)) return false;
    POINT upperLeft = {client.left, client.top};
    POINT lowerRight = {client.right, client.bottom};
    if (!ClientToScreen(g_window, &upperLeft) || !ClientToScreen(g_window, &lowerRight)) return false;
    *result = {upperLeft.x, upperLeft.y, lowerRight.x, lowerRight.y};
    return !IsRectEmpty(result);
}

bool IsContained(const RECT& clip, const RECT& bounds) {
    return !IsRectEmpty(&clip) && clip.left >= bounds.left && clip.top >= bounds.top &&
           clip.right <= bounds.right && clip.bottom <= bounds.bottom;
}

bool WindowReady() {
    return g_window != nullptr && IsWindow(g_window) && IsWindowVisible(g_window) &&
           !IsIconic(g_window) && GetForegroundWindow() == g_window && GetFocus() == g_window;
}

void LogSnapshot(const char* trigger) {
    if (g_window == nullptr || !IsWindow(g_window)) {
        logger::Log("WARN", "CursorClip", "trigger=%s status=window_invalid", trigger);
        return;
    }
    RECT client = {};
    RECT clip = {};
    POINT cursor = {};
    const bool clientValid = GetClientScreenRect(&client);
    const bool clipValid = GetClipCursor(&clip) != FALSE;
    const bool cursorValid = GetCursorPos(&cursor) != FALSE;
    RECT desktop = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                    GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                    GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    const char* status = "clip_query_failed";
    const char* canEscape = "unknown";
    if (clipValid && clientValid) {
        if (EqualRect(&clip, &desktop)) {
            status = "not_confined";
            canEscape = "yes";
        } else if (IsContained(clip, client)) {
            const bool full = std::abs(clip.left - client.left) <= 1 &&
                              std::abs(clip.top - client.top) <= 1 &&
                              std::abs(clip.right - client.right) <= 1 &&
                              std::abs(clip.bottom - client.bottom) <= 1;
            status = full ? "confined_to_full_client" : "confined_inside_client";
            canEscape = "no";
            if (!IsIconic(g_window)) {
                g_lastValidClip = clip;
                g_hasLastValidClip = true;
            }
        } else {
            status = "confined_to_other_rect";
            canEscape = "yes";
        }
    }
    const HMONITOR gameMonitor = MonitorFromWindow(g_window, MONITOR_DEFAULTTONULL);
    const HMONITOR cursorMonitor = cursorValid ? MonitorFromPoint(cursor, MONITOR_DEFAULTTONULL) : nullptr;
    MONITORINFOEXA gameInfo = {};
    MONITORINFOEXA cursorInfo = {};
    gameInfo.cbSize = sizeof(gameInfo);
    cursorInfo.cbSize = sizeof(cursorInfo);
    const bool gameValid = gameMonitor &&
        GetMonitorInfoA(gameMonitor, reinterpret_cast<MONITORINFO*>(&gameInfo));
    const bool cursorMonitorValid = cursorMonitor &&
        GetMonitorInfoA(cursorMonitor, reinterpret_cast<MONITORINFO*>(&cursorInfo));
    const LONG otherMonitor = gameValid && cursorMonitorValid ? (gameMonitor != cursorMonitor) : -1;
    const bool outside = cursorValid && clientValid && !PtInRect(&client, cursor);
    const bool transition = otherMonitor == 1 && g_lastOnOtherMonitor != 1;
    g_lastOnOtherMonitor = otherMonitor;
    logger::Log((outside && WindowReady()) || transition ? "WARN" : "INFO", "CursorClip",
                "trigger=%s status=%s can_escape=%s client=(%ld,%ld)-(%ld,%ld) "
                "clip=(%ld,%ld)-(%ld,%ld) desktop=(%ld,%ld)-(%ld,%ld) cursor=(%ld,%ld) "
                "outside_client=%d foreground=%d focus=%d visible=%d iconic=%d internal_resolution=%ldx%ld "
                "game_monitor=%s game_monitor_rect=(%ld,%ld)-(%ld,%ld) "
                "game_work=(%ld,%ld)-(%ld,%ld) game_primary=%d "
                "cursor_monitor=%s cursor_monitor_rect=(%ld,%ld)-(%ld,%ld) "
                "cursor_work=(%ld,%ld)-(%ld,%ld) cursor_primary=%d on_other_monitor=%ld",
                trigger, status, canEscape, client.left, client.top, client.right, client.bottom,
                clip.left, clip.top, clip.right, clip.bottom, desktop.left, desktop.top, desktop.right,
                desktop.bottom, cursor.x, cursor.y, outside, GetForegroundWindow() == g_window,
                GetFocus() == g_window, IsWindowVisible(g_window), IsIconic(g_window),
                *reinterpret_cast<volatile LONG*>(kScreenWidth),
                *reinterpret_cast<volatile LONG*>(kScreenHeight), gameValid ? gameInfo.szDevice : "unknown",
                gameInfo.rcMonitor.left, gameInfo.rcMonitor.top, gameInfo.rcMonitor.right,
                gameInfo.rcMonitor.bottom, gameInfo.rcWork.left, gameInfo.rcWork.top,
                gameInfo.rcWork.right, gameInfo.rcWork.bottom,
                gameValid && (gameInfo.dwFlags & MONITORINFOF_PRIMARY) != 0,
                cursorMonitorValid ? cursorInfo.szDevice : "unknown",
                cursorInfo.rcMonitor.left, cursorInfo.rcMonitor.top, cursorInfo.rcMonitor.right,
                cursorInfo.rcMonitor.bottom, cursorInfo.rcWork.left, cursorInfo.rcWork.top,
                cursorInfo.rcWork.right, cursorInfo.rcWork.bottom,
                cursorMonitorValid && (cursorInfo.dwFlags & MONITORINFOF_PRIMARY) != 0,
                otherMonitor);
}

bool TryRecover(const char* trigger) {
    if (!g_enabled) return true;
    if (!g_recoveryPending && g_ready) {
        RECT client = {};
        RECT clip = {};
        if (GetClientScreenRect(&client) && GetClipCursor(&clip) && IsContained(clip, client)) return true;
        logger::Log("WARN", "CursorClip", "active confinement lost; recovery requested by %s", trigger);
        g_recoveryPending = true;
        g_ready = false;
    }
    if (!WindowReady()) {
        logger::Log("INFO", "CursorClip", "recovery=deferred trigger=%s foreground=%d focus=%d visible=%d iconic=%d",
                    trigger, GetForegroundWindow() == g_window, GetFocus() == g_window,
                    g_window && IsWindowVisible(g_window), g_window && IsIconic(g_window));
        return false;
    }
    RECT requested = {};
    const char* source = "recalculated_client";
    if (!GetClientScreenRect(&requested)) {
        if (!g_hasLastValidClip) return false;
        requested = g_lastValidClip;
        source = "last_valid_clip";
    }
    SetLastError(0);
    const BOOL applied = ClipCursor(&requested);
    const DWORD error = applied ? ERROR_SUCCESS : GetLastError();
    RECT confirmed = {};
    RECT client = {};
    const bool valid = applied && GetClipCursor(&confirmed) && GetClientScreenRect(&client) &&
                       IsContained(confirmed, client);
    logger::Log(valid ? "INFO" : "ERROR", "CursorClip",
                "operation=restore source=%s requested=(%ld,%ld)-(%ld,%ld) "
                "confirmed=(%ld,%ld)-(%ld,%ld) result=%d error=%lu",
                source, requested.left, requested.top, requested.right, requested.bottom,
                confirmed.left, confirmed.top, confirmed.right, confirmed.bottom, valid, error);
    if (!valid) return false;
    g_lastValidClip = confirmed;
    g_hasLastValidClip = true;
    g_recoveryPending = false;
    g_ready = true;
    LogSnapshot("after_recovery");
    return true;
}
}  // namespace

void Initialize(bool enabled, HWND window) {
    g_enabled = enabled;
    g_window = window;
    g_ready = !enabled;
    g_recoveryPending = enabled;
    logger::Log("INFO", "RestoreCursorClip", "feature %s", enabled ? "enabled" : "disabled");
    if (enabled) {
        LogSnapshot("initialization");
        TryRecover("initialization");
    }
}

void HandleFocusLost() {
    if (!g_enabled) return;
    g_ready = false;
    g_recoveryPending = false;
    LogSnapshot("before_focus_loss");
    if (g_hasLastValidClip) {
        logger::Log("INFO", "CursorClip", "preserving last_valid=(%ld,%ld)-(%ld,%ld)",
                    g_lastValidClip.left, g_lastValidClip.top,
                    g_lastValidClip.right, g_lastValidClip.bottom);
    }
    const BOOL result = ClipCursor(nullptr);
    logger::Log(result ? "INFO" : "ERROR", "CursorClip", "operation=release result=%d error=%lu",
                result, result ? 0 : GetLastError());
    LogSnapshot("after_focus_loss");
}

bool HandleFocusGained(const char* trigger) {
    if (!g_enabled) return true;
    g_recoveryPending = true;
    return TryRecover(trigger);
}

void HandleWindowChanged(const char* trigger) {
    if (!g_enabled) return;
    LogSnapshot(trigger);
    TryRecover(trigger);
}

void Shutdown() {
    g_ready = false;
    g_recoveryPending = false;
    g_window = nullptr;
}

bool IsEnabled() { return g_enabled; }
bool IsReady() { return !g_enabled || g_ready; }

}  // namespace cursor_clip
