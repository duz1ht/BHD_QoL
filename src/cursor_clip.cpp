#include "cursor_clip.h"

#include <cstdint>

#include "cursor_clip_math.h"

namespace cursor_clip {
namespace {
constexpr uintptr_t kScreenWidth = 0x009F72C0;
constexpr uintptr_t kScreenHeight = 0x009F72C4;

bool g_enabled = false;
bool g_ready = false;
bool g_recoveryPending = false;
bool g_focusSuspended = false;
HWND g_window = nullptr;
RECT g_lastValidClip = {};
bool g_hasLastValidClip = false;

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

RectEdges ToRectEdges(const RECT& rect) {
    return {rect.left, rect.top, rect.right, rect.bottom};
}

bool RectCoversFullClient(const RECT& clip, const RECT& client) {
    return cursor_clip::CoversFullClient(ToRectEdges(clip), ToRectEdges(client));
}

bool WindowReady() {
    return g_window != nullptr && IsWindow(g_window) && IsWindowVisible(g_window) &&
           !IsIconic(g_window) && GetForegroundWindow() == g_window && GetFocus() == g_window;
}


bool TryRecover() {
    if (!g_enabled) return true;
    if (!g_recoveryPending && g_ready) {
        RECT client = {};
        RECT clip = {};
        if (GetClientScreenRect(&client) && GetClipCursor(&clip) &&
            RectCoversFullClient(clip, client)) return true;
        g_recoveryPending = true;
        g_ready = false;
    }
    if (!WindowReady()) {
        return false;
    }
    RECT requested = {};
    if (!GetClientScreenRect(&requested)) {
        if (!g_hasLastValidClip) return false;
        requested = g_lastValidClip;
    }
    const BOOL applied = ClipCursor(&requested);
    RECT confirmed = {};
    RECT client = {};
    const bool valid = applied && GetClipCursor(&confirmed) && GetClientScreenRect(&client) &&
                       RectCoversFullClient(confirmed, client);
    if (!valid) return false;
    g_lastValidClip = confirmed;
    g_hasLastValidClip = true;
    g_recoveryPending = false;
    g_ready = true;
    g_focusSuspended = false;
    return true;
}
}  // namespace

void Initialize(bool enabled, HWND window) {
    g_enabled = enabled;
    g_window = window;
    g_ready = !enabled;
    g_recoveryPending = enabled;
    g_focusSuspended = false;
    if (enabled) {
        TryRecover();
    }
}

void HandleFocusLost() {
    if (!g_enabled || g_focusSuspended) return;
    g_focusSuspended = true;
    g_ready = false;
    g_recoveryPending = false;
    ClipCursor(nullptr);
}

bool HandleFocusGained() {
    if (!g_enabled) return true;
    if (!g_ready) g_recoveryPending = true;
    return TryRecover();
}

void HandleWindowChanged() {
    if (!g_enabled) return;
    TryRecover();
}

void Shutdown() {
    g_ready = false;
    g_recoveryPending = false;
    g_focusSuspended = false;
    g_window = nullptr;
}

bool IsEnabled() { return g_enabled; }
bool IsReady() { return !g_enabled || g_ready; }

}  // namespace cursor_clip
