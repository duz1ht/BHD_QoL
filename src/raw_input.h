#pragma once

namespace raw_input {

struct Settings {
    bool enabled;
    unsigned long statisticsIntervalMs;
};

// Installs the validated PollMouseInput detour. Window discovery and Raw Input
// registration are deferred until the game reaches its mouse-polling thread.
bool Install(const Settings& settings);

}  // namespace raw_input
