#pragma once

#include <cstdint>

namespace visual_camera_math {

constexpr uint32_t kAlphaOne = 65536;

struct TransformQ16 {
    int32_t x, y, z;
    uint32_t yaw, pitch, roll;
};

uint32_t Alpha250(uint32_t remainder);
uint32_t Alpha62(uint32_t remainder, uint32_t postIncrementPhase);
int32_t LerpQ16(int32_t from, int32_t to, uint32_t alphaQ16);
uint32_t LerpAngle(uint32_t from, uint32_t to, uint32_t alphaQ16);
int32_t NewCumulativeCounts(uint32_t total, uint32_t alreadyVisualized);
int32_t UnfilteredYawStep(int32_t yawAction);
TransformQ16 Interpolate(const TransformQ16& previous, const TransformQ16& current,
                         uint32_t alphaQ16);
bool IsTeleport(const TransformQ16& previous, const TransformQ16& current,
                int32_t thresholdQ16 = 64 * 65536);

} // namespace visual_camera_math
