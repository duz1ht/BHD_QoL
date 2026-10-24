#pragma once

#include <cstdint>

namespace high_frequency_camera {

int32_t PendingYawOffset(int16_t scaledCommand);
int32_t PendingPitchOffset(int16_t scaledCommand);
int32_t ClampPitch(int64_t pitch);

} // namespace high_frequency_camera
