#include "visual_camera_math.h"

#include <algorithm>
#include <cstdint>

namespace visual_camera_math {

uint32_t Alpha250(uint32_t remainder) {
    return std::min(remainder, 64u) * kAlphaOne / 64u;
}

uint32_t Alpha62(uint32_t remainder, uint32_t postIncrementPhase) {
    const uint32_t completedQuarters = (postIncrementPhase - 1u) & 3u;
    const uint32_t units = completedQuarters * 64u + std::min(remainder, 64u);
    return std::min(units * kAlphaOne / 256u, kAlphaOne);
}

int32_t LerpQ16(int32_t from, int32_t to, uint32_t alphaQ16) {
    const int64_t delta = static_cast<int64_t>(to) - from;
    return static_cast<int32_t>(from + delta * std::min(alphaQ16, kAlphaOne) / kAlphaOne);
}

uint32_t LerpAngle(uint32_t from, uint32_t to, uint32_t alphaQ16) {
    const uint32_t modular = to - from;
    const int64_t shortest = modular <= 0x7fffffffu
                                 ? static_cast<int64_t>(modular)
                                 : static_cast<int64_t>(modular) - 0x100000000LL;
    return from + static_cast<uint32_t>(shortest * std::min(alphaQ16, kAlphaOne) / kAlphaOne);
}

TransformQ16 Interpolate(const TransformQ16& a, const TransformQ16& b, uint32_t alpha) {
    return {LerpQ16(a.x, b.x, alpha), LerpQ16(a.y, b.y, alpha), LerpQ16(a.z, b.z, alpha),
            LerpAngle(a.yaw, b.yaw, alpha), LerpAngle(a.pitch, b.pitch, alpha),
            LerpAngle(a.roll, b.roll, alpha)};
}

bool IsTeleport(const TransformQ16& a, const TransformQ16& b, int32_t threshold) {
    const auto exceeds = [threshold](int32_t from, int32_t to) {
        int64_t delta = static_cast<int64_t>(to) - from;
        if (delta < 0) delta = -delta;
        return delta > threshold;
    };
    return exceeds(a.x, b.x) || exceeds(a.y, b.y) || exceeds(a.z, b.z);
}
} // namespace visual_camera_math
