#include "high_frequency_camera_math.h"

namespace high_frequency_camera {

int16_t AdvanceYawFilter(int16_t state, int16_t command) {
    const int16_t sum = static_cast<int16_t>(state + command);
    return static_cast<int16_t>(sum - static_cast<int16_t>((sum + 4) >> 3));
}

int32_t PendingYawOffset(int16_t filterStart, int16_t command) {
    const int16_t noInput = AdvanceYawFilter(filterStart, 0);
    const int16_t withInput = AdvanceYawFilter(filterStart, command);
    return -static_cast<int32_t>(withInput - noInput) * 0xC00;
}

int32_t PendingPitchOffset(int16_t command, bool applyNativePitchScale) {
    int32_t offset = static_cast<int32_t>(command) * 0x4000;
    if (applyNativePitchScale) {
        offset = static_cast<int32_t>(
            (static_cast<int64_t>(offset) * 0x21000) >> 16);
    }
    return offset;
}

} // namespace high_frequency_camera
