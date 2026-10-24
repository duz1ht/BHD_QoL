#pragma once

#include <windows.h>

#include <cstdint>

namespace raw_input {

struct Settings {
    bool enabled;
    unsigned long statisticsIntervalMs;
};

struct TraceSnapshot {
    unsigned long pollSerial;
    unsigned long rawPacketSerial;
    unsigned long directPacketSerial;
    unsigned long bufferedPacketSerial;
    unsigned long bufferDrainSerial;
    unsigned long bufferReadFailures;
    LONG consumedX;
    LONG consumedY;
    int32_t cameraYawAtPoll;
    int32_t cameraPitchAtPoll;
};

bool Install(const Settings& settings);
bool AttachWindow(HWND window);
void HandleRawInput(HRAWINPUT input);
void HandleFocusLost();
bool HandleFocusGained(const char* trigger, bool clipReady);
void HandleDestroy();
bool IsEnabled();
bool GetPendingMovement(LONG* x, LONG* y);
bool GetProjectedNativeMovement(LONG* x, LONG* y, uint32_t* phaseQ16 = nullptr);
unsigned long GetSimulationSerial();
void GetTraceCounters(unsigned long* pollSerial, unsigned long* rawPacketSerial);
void GetTraceSnapshot(TraceSnapshot* snapshot);

}  // namespace raw_input
