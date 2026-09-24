#pragma once

#include <windows.h>

namespace raw_input {

struct Settings {
    bool enabled;
    unsigned long statisticsIntervalMs;
};

struct PredictionSnapshot {
    LONG totalX;
    LONG totalY;
    LONG committedX;
    LONG committedY;
};

bool Install(const Settings& settings);
bool AttachWindow(HWND window);
void HandleRawInput(HRAWINPUT input);
void HandleFocusLost();
bool HandleFocusGained(const char* trigger, bool clipReady);
void HandleDestroy();
bool IsEnabled();
PredictionSnapshot GetPredictionSnapshot();
void ResetPrediction();

}  // namespace raw_input
