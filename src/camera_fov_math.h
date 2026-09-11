#pragma once

#include <cstdint>

namespace camera_fov {

constexpr int32_t kVanillaFovQ16 = 0x00500000;

// Preserves the vertical framing of an 80-degree horizontal FOV at 4:3.
// Invalid, 4:3, narrower, and implausibly wide inputs remain exactly vanilla.
int32_t CorrectHorizontalFovQ16(int32_t width, int32_t height);

}  // namespace camera_fov
