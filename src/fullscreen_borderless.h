#pragma once

#include <windows.h>

namespace fullscreen_borderless {

// Selects the primary monitor and forces the game's windowed render path and
// internal resolution before the game creates its Direct3D device.
bool Initialize(bool enabled);

// Applies (or re-applies) the borderless frame and full-monitor geometry.
bool Apply(HWND window, const char* trigger);

}  // namespace fullscreen_borderless
