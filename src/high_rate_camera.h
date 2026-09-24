#pragma once

namespace high_rate_camera {

struct Settings {
    bool enabled;
    unsigned long statisticsIntervalMs;
};

bool Install(const Settings& settings);
void Reset();

}  // namespace high_rate_camera
