#pragma once

#include <windows.h>

namespace cursor_clip {

void Initialize(bool enabled, HWND window);
void HandleFocusLost();
bool HandleFocusGained();
void HandleWindowChanged();
void Shutdown();
bool IsEnabled();
bool IsReady();

}  // namespace cursor_clip
