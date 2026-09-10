#pragma once

#include <windows.h>

namespace game_window {

struct Settings {
    bool rawMouseInput;
    bool restoreCursorClip;
};

void Configure(const Settings& settings);
bool EnsureInstalled(HWND window);
void HandleStatisticsInterval();

}  // namespace game_window
