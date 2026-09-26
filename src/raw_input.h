#pragma once

#include <windows.h>
#include <cstdint>

namespace raw_input {

struct Settings {
    bool enabled;
    unsigned long statisticsIntervalMs;
};

struct PredictionSnapshot {
    uint32_t totalX;
    uint32_t totalY;
    uint32_t committedX;
    uint32_t committedY;
    uint32_t logicPollSerial;
};

bool Install(const Settings& settings);
bool AttachWindow(HWND window);
void HandleRawInput(HRAWINPUT input);
void HandleFocusLost();
bool HandleFocusGained(const char* trigger, bool clipReady);
void HandleDestroy();
bool IsEnabled();
bool IsActive();
bool PollForRenderFrame();
PredictionSnapshot GetPredictionSnapshot();
void GetLastRenderDelta(LONG* x, LONG* y);

}  // namespace raw_input
