#include "cursor_visibility.h"

#include <windows.h>


namespace cursor_visibility {
namespace {
constexpr int kMaximumAdjustments = 32;
int g_showAdjustments = 0;
bool g_deactivated = false;
}  // namespace

void HandleGameDeactivated() {
    if (g_deactivated) return;
    g_deactivated = true;
    g_showAdjustments = 0;

    int displayCount = ShowCursor(TRUE);
    ++g_showAdjustments;
    while (displayCount < 0 && g_showAdjustments < kMaximumAdjustments) {
        displayCount = ShowCursor(TRUE);
        ++g_showAdjustments;
    }
}

void HandleGameActivated() {
    if (!g_deactivated) return;
    for (int adjustment = 0; adjustment < g_showAdjustments; ++adjustment) {
        ShowCursor(FALSE);
    }
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
