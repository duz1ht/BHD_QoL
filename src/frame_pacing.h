#pragma once

#include <windows.h>

namespace frame_pacing {

struct Settings {
    bool enabled;
    unsigned long statisticsIntervalMs;
};

bool Install(const Settings& settings);
void NotifyInputPoll(LONG64 counterTick);

}  // namespace frame_pacing
