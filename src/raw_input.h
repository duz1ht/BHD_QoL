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

struct PredictionSnapshot {
    LONG totalX;
    LONG totalY;
    LONG committedX;
    LONG committedY;
    LONG logicPollSerial;
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
PredictionSnapshot GetPredictionSnapshot();

}  // namespace raw_input
