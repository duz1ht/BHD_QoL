#include "cursor_visibility.h"

#include <windows.h>

#include "logger.h"

namespace cursor_visibility {
namespace {
constexpr int kMaximumAdjustments = 32;
int g_showAdjustments = 0;
bool g_deactivated = false;
}  // namespace

void HandleGameDeactivated(const char* trigger) {
    if (g_deactivated) return;
    g_deactivated = true;
    g_showAdjustments = 0;

    int displayCount = ShowCursor(TRUE);
    ++g_showAdjustments;
    while (displayCount < 0 && g_showAdjustments < kMaximumAdjustments) {
        displayCount = ShowCursor(TRUE);
        ++g_showAdjustments;
    }
    logger::Log(displayCount >= 0 ? "INFO" : "WARN", "CursorVisibility",
                "trigger=%s action=show adjustments=%d display_count=%d",
                trigger, g_showAdjustments, displayCount);
}

void HandleGameActivated(const char* trigger) {
    if (!g_deactivated) return;
    int displayCount = 0;
    for (int adjustment = 0; adjustment < g_showAdjustments; ++adjustment) {
        displayCount = ShowCursor(FALSE);
    }
    logger::Log("INFO", "CursorVisibility",
                "trigger=%s action=restore adjustments=%d display_count=%d",
                trigger, g_showAdjustments, displayCount);
    g_showAdjustments = 0;
    g_deactivated = false;
}

void Shutdown() {
    // If the game closes while inactive, retain the compensating increments so
    // the desktop cursor cannot be hidden by teardown of the game window.
    g_showAdjustments = 0;
    g_deactivated = false;
}

}  // namespace cursor_visibility
