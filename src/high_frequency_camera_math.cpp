#include "high_frequency_camera_math.h"

namespace high_frequency_camera {

int32_t PendingYawOffset(int16_t scaledCommand) {
    return -static_cast<int32_t>(scaledCommand) * 0x10000;
}

int32_t PendingPitchOffset(int16_t scaledCommand) {
    return static_cast<int32_t>(scaledCommand) * 0x10000;
}

int32_t ClampPitch(int64_t pitch) {
    constexpr int32_t kMinimumPitch = -0x38E38E00;
    constexpr int32_t kMaximumPitch = 0x38E38E00;
    if (pitch < kMinimumPitch) return kMinimumPitch;
    if (pitch > kMaximumPitch) return kMaximumPitch;
    return static_cast<int32_t>(pitch);
}

} // namespace high_frequency_camera
