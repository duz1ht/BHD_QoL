#pragma once

#include <cstdint>

namespace high_frequency_camera {

int16_t AdvanceYawFilter(int16_t state, int16_t command);
int32_t PendingYawOffset(int16_t filterStart, int16_t command);
int32_t PendingPitchOffset(int16_t command, bool applyNativePitchScale);

} // namespace high_frequency_camera
