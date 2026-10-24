#pragma once

#include <cstdint>

namespace high_frequency_camera {

bool Apply(void* sourceCamera);
void SetEnabled(bool enabled);
bool GetPresentationCameraPose(int32_t simulationPose[6], int32_t presentationPose[6]);

} // namespace high_frequency_camera
