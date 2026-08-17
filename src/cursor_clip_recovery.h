#pragma once

#include <windows.h>

namespace cursor_clip_recovery {

struct Settings {
    bool enabled = true;
    bool waitForDisplayChange = true;
    UINT delayMs = 250;
};

bool Install(const Settings& settings, HANDLE stopEvent);
void Remove();

}  // namespace cursor_clip_recovery
