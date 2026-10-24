#pragma once

#include <windows.h>

#include <cstdint>

#include "high_frequency_camera.h"
#include "mouse_scaling_fix.h"
#include "raw_input.h"

namespace camera_trace {

struct Sample {
    void* sourceCamera;
    void* destinationCamera;
    unsigned long renderSerial;
    unsigned long pollSerial;
    unsigned long rawPacketSerial;
    raw_input::TraceSnapshot input;
    mouse_scaling_fix::TraceSnapshot scaling;
    LONG pendingRawX;
    LONG pendingRawY;
    high_frequency_camera::ApplyResult apply;
    int32_t sourceYawBefore;
    int32_t sourcePitchBefore;
    int32_t sourceYawPreview;
    int32_t sourcePitchPreview;
    int32_t destinationYaw;
    int32_t destinationPitch;
    int32_t sourceYawRestored;
    int32_t sourcePitchRestored;
};

bool Initialize(bool enabled);
bool IsEnabled();
void UpdateHotkey();
void Record(const Sample& sample);

}  // namespace camera_trace
