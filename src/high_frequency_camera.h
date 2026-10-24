#pragma once

#include <windows.h>

#include <cstdint>

namespace high_frequency_camera {

bool Apply(void* sourceCamera);
void SetEnabled(bool enabled);
bool GetPresentationOffset(int32_t* yawOffset, int32_t* pitchOffset);
bool GetPresentationCameraPose(int32_t simulationPose[6], int32_t presentationPose[6]);

} // namespace high_frequency_camera
