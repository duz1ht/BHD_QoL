#pragma once

#include <windows.h>

namespace raw_input {

struct Settings {
    bool enabled;
    unsigned long statisticsIntervalMs;
};

struct RenderMouseDelta {
    LONG x;
    LONG y;
};

bool Install(const Settings& settings);
bool AttachWindow(HWND window);
void HandleRawInput(HRAWINPUT input);
void HandleFocusLost();
bool HandleFocusGained(const char* trigger, bool clipReady);
void HandleDestroy();
bool IsEnabled();
bool IsBackendActive();
void PollForRenderFrame();
RenderMouseDelta GetLastRenderDelta();

}  // namespace raw_input
