#pragma once

#include <windows.h>

namespace borderless_fullscreen {

// Selects the primary monitor and forces the game's windowed render path. The
// internal resolution is changed to the monitor size only when requested.
bool Initialize(bool enabled, bool forceDesktopResolution);

// Applies (or re-applies) the borderless frame and full-monitor geometry.
bool Apply(HWND window, const char* trigger);

}  // namespace borderless_fullscreen
