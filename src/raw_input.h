#pragma once

#include <windows.h>

#include <cstdint>

namespace raw_input {

struct PendingVisualMouse {
    int64_t x;
    int64_t y;
    uint64_t captureSequence;
    uint64_t consumedSequence;
    bool valid;
};

struct Settings {
    bool enabled;
    unsigned long statisticsIntervalMs;
};

bool Install(const Settings& settings);
bool AttachWindow(HWND window);
void HandleRawInput(HRAWINPUT input);
void HandleFocusLost();
bool HandleFocusGained(const char* trigger, bool clipReady);
void HandleDestroy();
bool IsEnabled();
PendingVisualMouse GetPendingVisualMouse();

}  // namespace raw_input
