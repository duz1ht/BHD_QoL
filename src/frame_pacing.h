#pragma once

#include <windows.h>

namespace frame_pacing {

struct Settings {
    bool diagnosticsEnabled;
    unsigned long statisticsIntervalMs;
    unsigned int renderFrameLimit;
};

bool Install(const Settings& settings);
void NotifyInputPoll(LONG64 counterTick);

}  // namespace frame_pacing
