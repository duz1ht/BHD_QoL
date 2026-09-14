#pragma once

#include <cstdint>

namespace visual_camera {

constexpr std::uint32_t kInterpolationOne = 65536;

inline std::int32_t InterpolateLinear(std::int32_t from, std::int32_t to,
                                      std::uint32_t alphaQ16) {
    if (alphaQ16 >= kInterpolationOne) return to;
    const std::int64_t delta = static_cast<std::int64_t>(to) - from;
    return static_cast<std::int32_t>(from + delta * alphaQ16 / kInterpolationOne);
}

// DFBHD angles use the complete unsigned 32-bit turn. Taking the shorter
// modular arc prevents a north-crossing from rotating almost a full turn.
inline std::uint32_t InterpolateAngle(std::uint32_t from, std::uint32_t to,
                                      std::uint32_t alphaQ16) {
    if (alphaQ16 >= kInterpolationOne) return to;
    const std::uint32_t modularDelta = to - from;
    const std::int64_t shortestDelta = modularDelta <= 0x7fffffffu
        ? static_cast<std::int64_t>(modularDelta)
        : static_cast<std::int64_t>(modularDelta) - 0x100000000ll;
    return from + static_cast<std::uint32_t>(
        shortestDelta * alphaQ16 / kInterpolationOne);
}

inline std::uint32_t InterpolationAlpha(std::int64_t elapsedTicks,
                                        std::int64_t durationTicks) {
    if (elapsedTicks <= 0 || durationTicks <= 0) return 0;
    if (elapsedTicks >= durationTicks) return kInterpolationOne;
    return static_cast<std::uint32_t>(elapsedTicks * kInterpolationOne / durationTicks);
}

}  // namespace visual_camera
