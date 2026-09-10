#pragma once

#include <windows.h>

namespace cursor_clip {

void Initialize(bool enabled, HWND window);
void HandleFocusLost();
bool HandleFocusGained(const char* trigger);
void HandleWindowChanged(const char* trigger);
void Shutdown();
bool IsEnabled();
bool IsReady();

}  // namespace cursor_clip
