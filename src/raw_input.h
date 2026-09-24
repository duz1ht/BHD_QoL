#pragma once

#include <windows.h>

namespace raw_input {

struct Settings {
    bool enabled;
    unsigned long statisticsIntervalMs;
};

struct MovementSnapshot {
    long x;
    long y;
    unsigned long epoch;
    bool active;
};

bool Install(const Settings& settings);
bool AttachWindow(HWND window);
void HandleRawInput(HRAWINPUT input);
void HandleFocusLost();
bool HandleFocusGained(const char* trigger, bool clipReady);
void HandleDestroy();
bool IsEnabled();
MovementSnapshot GetMovementSnapshot();

}  // namespace raw_input
