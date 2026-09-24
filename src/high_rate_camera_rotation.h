#pragma once

#include <cstdint>

namespace high_rate_camera_rotation {

struct VisualLookOrientation {
    bool valid;
    uint32_t frameSerial;
    uint32_t yaw;
    uint32_t pitch;
};

bool Install(bool enabled, bool freeRateMousePollAvailable,
             bool mouseScalingFixEnabled, unsigned long statisticsIntervalMs);
void EvaluateAndApplyForCurrentFrame();
void Invalidate();
VisualLookOrientation GetVisualLookOrientation();

} // namespace high_rate_camera_rotation
